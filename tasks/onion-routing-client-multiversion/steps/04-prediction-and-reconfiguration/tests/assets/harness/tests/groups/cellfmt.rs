//! Group `cellfmt` (inherited) — basic channel and relay cell layout: command
//! numbers, circuit IDs, stream IDs, the relay header, and the documented
//! offsets where those fields sit in a cell body.
//!
//! Every expected value here is stated in `SPEC_NOTES.md` sections 1 and 2;
//! nothing is obtained by encoding with the candidate's code and decoding it
//! again with the same code.

use crate::common;
use tor_cell::chancell::msg::{ChanMsg, Destroy, DestroyReason};
use tor_cell::chancell::{ChanCell, ChanCmd, CircId};
use tor_cell::relaycell::msg::{Begin, RelayMsg};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

/// The wire number of a channel command.
fn chan_num(cmd: ChanCmd) -> u8 {
    cmd.into()
}

/// The wire number of a relay command.
fn relay_num(cmd: RelayCmd) -> u8 {
    cmd.into()
}

#[test]
fn cellfmt__channel_commands_have_the_documented_numbers() {
    assert_eq!(chan_num(ChanCmd::PADDING), 0);
    assert_eq!(chan_num(ChanCmd::CREATE), 1);
    assert_eq!(chan_num(ChanCmd::CREATED), 2);
    assert_eq!(chan_num(ChanCmd::RELAY), 3);
    assert_eq!(chan_num(ChanCmd::DESTROY), 4);
    assert_eq!(chan_num(ChanCmd::CREATE_FAST), 5);
    assert_eq!(chan_num(ChanCmd::CREATED_FAST), 6);
    assert_eq!(chan_num(ChanCmd::NETINFO), 8);
    assert_eq!(chan_num(ChanCmd::RELAY_EARLY), 9);
    assert_eq!(chan_num(ChanCmd::CREATE2), 10);
    assert_eq!(chan_num(ChanCmd::CREATED2), 11);
    // ...and the numbers map back to the same commands.
    assert_eq!(ChanCmd::from(3), ChanCmd::RELAY);
    assert_eq!(ChanCmd::from(4), ChanCmd::DESTROY);
}

#[test]
fn cellfmt__relay_commands_have_the_documented_numbers() {
    assert_eq!(relay_num(RelayCmd::BEGIN), 1);
    assert_eq!(relay_num(RelayCmd::DATA), 2);
    assert_eq!(relay_num(RelayCmd::END), 3);
    assert_eq!(relay_num(RelayCmd::CONNECTED), 4);
    assert_eq!(relay_num(RelayCmd::SENDME), 5);
    assert_eq!(relay_num(RelayCmd::EXTEND), 6);
    assert_eq!(relay_num(RelayCmd::EXTENDED), 7);
    assert_eq!(relay_num(RelayCmd::TRUNCATE), 8);
    assert_eq!(relay_num(RelayCmd::TRUNCATED), 9);
    assert_eq!(relay_num(RelayCmd::DROP), 10);
    assert_eq!(relay_num(RelayCmd::RESOLVE), 11);
    assert_eq!(relay_num(RelayCmd::RESOLVED), 12);
    assert_eq!(relay_num(RelayCmd::BEGIN_DIR), 13);
    assert_eq!(RelayCmd::from(2), RelayCmd::DATA);
    assert_eq!(RelayCmd::from(13), RelayCmd::BEGIN_DIR);
}

#[test]
fn cellfmt__a_channel_cell_carries_its_circuit_id_and_message() {
    let cell = ChanCell::new(
        CircId::from(0x1234_5678),
        ChanMsg::Destroy(Destroy::new(DestroyReason::NONE)),
    );
    assert_eq!(cell.circid(), CircId::from(0x1234_5678));
    assert_eq!(cell.msg().cmd(), ChanCmd::DESTROY);

    let (circ_id, msg) = cell.into_circid_and_msg();
    assert_eq!(u32::from(circ_id), 0x1234_5678);
    match msg {
        // Deliberately NOT asserting on d.reason(): the accessor is introduced
        // by THIS step (tor-cell chancell/msg.rs), so reading it here would put
        // new capability behind a gate group and veto every candidate that has
        // not implemented it yet. The reason code is priced by the scored group
        // `destroy_reason`. What this case is about is the circid/message
        // round-trip, which is inherited behaviour.
        ChanMsg::Destroy(_) => {}
        other => panic!("message is not a DESTROY: {:?}", other.cmd()),
    }
}

#[test]
fn cellfmt__a_relay_cell_carries_its_stream_id_and_message() {
    let begin = Begin::new("example.com", 443, 0).expect("begin cell should build");
    let cell = RelayCell::new(StreamId::from(0x9abc), RelayMsg::Begin(begin));
    assert_eq!(cell.cmd(), RelayCmd::BEGIN);

    let (stream_id, msg) = cell.into_streamid_and_msg();
    assert_eq!(stream_id, StreamId::from(0x9abc));
    assert_eq!(u16::from(stream_id), 0x9abc);
    assert_eq!(msg.cmd(), RelayCmd::BEGIN);

    // The same two fields recovered from a BEGIN cell captured as a file, so
    // the bytes the decoder is handed were not produced by the candidate's own
    // encoder. Its target is `www.example.com:443` and it runs on stream 1234.
    let fixture = common::body_from_fixture(include_bytes!("../../fixtures/relay_begin_cell.bin"));
    assert_eq!(fixture[0], relay_num(RelayCmd::BEGIN), "byte 0 is the command");
    assert_eq!(&fixture[3..5], &[0x04, 0xd2], "bytes 3..5 are the stream id");
    assert_eq!(
        &fixture[11..31],
        b"www.example.com:443\0",
        "the address and its terminator start at byte 11"
    );
    let (stream_id, msg) = RelayCell::decode(fixture)
        .expect("the captured BEGIN cell should decode")
        .into_streamid_and_msg();
    assert_eq!(stream_id, StreamId::from(1234));
    assert_eq!(msg.cmd(), RelayCmd::BEGIN);
}

#[test]
fn cellfmt__the_relay_header_sits_at_the_documented_offsets() {
    let body = common::relay_body(
        relay_num(RelayCmd::DATA),
        0,
        0x1234,
        [0xaa, 0xbb, 0xcc, 0xdd],
        b"payload",
    );
    assert_eq!(body[0], 2, "relay command is the first byte");
    assert_eq!(&body[1..3], &[0, 0], "recognized occupies bytes 1..3");
    assert_eq!(&body[3..5], &[0x12, 0x34], "stream id occupies bytes 3..5");
    assert_eq!(
        &body[5..9],
        &[0xaa, 0xbb, 0xcc, 0xdd],
        "digest occupies bytes 5..9"
    );
    assert_eq!(&body[9..11], &[0, 7], "length occupies bytes 9..11");
    assert_eq!(&body[11..18], b"payload", "the payload starts at byte 11");

    // The decoder reads exactly those fields back out.
    let decoded = RelayCell::decode(body).expect("hand-built cell should decode");
    assert_eq!(decoded.cmd(), RelayCmd::DATA);
    match decoded.msg() {
        RelayMsg::Data(d) => assert_eq!(d.as_ref(), b"payload"),
        other => panic!("decoded message is not DATA: {:?}", other.cmd()),
    }

    // The same layout, read back out of a cell captured as a file rather than
    // assembled here: a DATA cell carrying a GET request on stream 257.
    // Feeding the decoder bytes this test did not build is what rules out an
    // encoder and a decoder that agree with each other and with nothing else.
    let fixture = common::body_from_fixture(include_bytes!("../../fixtures/relay_data_cell.bin"));
    assert_eq!(fixture[0], relay_num(RelayCmd::DATA), "byte 0 is the command");
    assert_eq!(&fixture[1..3], &[0, 0], "bytes 1..3 are `recognized`");
    assert_eq!(&fixture[3..5], &[0x01, 0x01], "bytes 3..5 are the stream id");
    assert_eq!(&fixture[9..11], &[0x00, 0x12], "bytes 9..11 are the length");
    let (stream_id, msg) = RelayCell::decode(fixture)
        .expect("the captured DATA cell should decode")
        .into_streamid_and_msg();
    assert_eq!(stream_id, StreamId::from(0x0101));
    match msg {
        RelayMsg::Data(d) => assert_eq!(d.as_ref(), b"GET / HTTP/1.0\r\n\r\n"),
        other => panic!("captured message is not DATA: {:?}", other.cmd()),
    }
}

#[test]
fn cellfmt__the_command_field_is_one_byte() {
    // One minimal well-formed payload per command. Byte zero, on its own, is
    // what selects the message the remaining bytes are read as.
    let cases: [(RelayCmd, &[u8]); 6] = [
        (RelayCmd::BEGIN, b"example.com:443\0"),
        (RelayCmd::DATA, b"hello"),
        (RelayCmd::END, &[]),
        (RelayCmd::SENDME, &[]),
        (RelayCmd::TRUNCATE, &[]),
        (RelayCmd::DROP, &[]),
    ];
    for (cmd, payload) in cases {
        let body = common::relay_body(relay_num(cmd), 0, 0, [0; 4], payload);
        assert_eq!(body[0], relay_num(cmd));
        let decoded = RelayCell::decode(body).expect("cell should decode");
        assert_eq!(decoded.cmd(), cmd);
        assert_eq!(decoded.msg().cmd(), cmd);
    }

    // The command field is exactly one byte wide: rewriting byte zero and
    // nothing else re-reads the very same payload as a different message.
    let mut body = common::relay_body(relay_num(RelayCmd::DATA), 0, 0, [0; 4], b"hello");
    assert_eq!(
        RelayCell::decode(body).expect("cell should decode").cmd(),
        RelayCmd::DATA
    );
    body[0] = relay_num(RelayCmd::DROP);
    assert_eq!(
        RelayCell::decode(body).expect("cell should decode").cmd(),
        RelayCmd::DROP
    );
}

#[test]
fn cellfmt__the_stream_id_field_is_two_bytes() {
    for id in [0_u16, 1, 255, 256, 32768, u16::MAX] {
        let body = common::relay_body(relay_num(RelayCmd::DATA), 0, id, [0; 4], &[]);
        assert_eq!(&body[3..5], &id.to_be_bytes()[..]);
        let decoded = RelayCell::decode(body).expect("cell should decode");
        let (stream_id, _) = decoded.into_streamid_and_msg();
        assert_eq!(stream_id, StreamId::from(id));
    }
}

#[test]
fn cellfmt__the_length_field_reports_the_payload_size() {
    let payloads: [&[u8]; 4] = [b"", b"x", b"12345", &[99_u8; 400]];
    for payload in payloads {
        let body = common::relay_body(relay_num(RelayCmd::DATA), 0, 1, [0; 4], payload);
        let len = u16::from_be_bytes([body[9], body[10]]);
        assert_eq!(len as usize, payload.len());
        // ...and that is the number of payload bytes the decoder hands back.
        let decoded = RelayCell::decode(body).expect("cell should decode");
        match decoded.msg() {
            RelayMsg::Data(d) => assert_eq!(d.as_ref().len(), payload.len()),
            other => panic!("decoded message is not DATA: {:?}", other.cmd()),
        }
    }
}

#[test]
fn cellfmt__a_length_beyond_the_cell_is_refused() {
    // The length field claims more payload than a 509-byte body can hold.
    let mut body = common::relay_body(relay_num(RelayCmd::DATA), 0, 1, [0; 4], b"short");
    body[9..11].copy_from_slice(&600_u16.to_be_bytes());
    assert!(
        RelayCell::decode(body).is_err(),
        "a length past the end of the cell must not decode"
    );
}

#[test]
fn cellfmt__zero_padding_fills_the_cell_to_a_fixed_length() {
    let body = common::relay_body(relay_num(RelayCmd::DATA), 0, 0, [0; 4], b"short");
    assert_eq!(body.len(), common::CELL_BODY_LEN);
    assert_eq!(common::CELL_BODY_LEN, 509);
    for (i, byte) in body
        .iter()
        .enumerate()
        .skip(common::RELAY_HEADER_LEN + b"short".len())
    {
        assert_eq!(*byte, 0, "byte at offset {} is not zero", i);
    }
}
