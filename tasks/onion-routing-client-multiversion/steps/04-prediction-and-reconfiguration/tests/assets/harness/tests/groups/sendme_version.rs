//! Group `sendme_version` — a SENDME cell may carry an authentication tag, and
//! a version field this implementation does not know is an error rather than a
//! silent success.
//!
//! The change is in this step: the previous checkpoint dropped the tag and
//! accepted the cell when it read a version it did not recognize.

use crate::common;
use rand::SeedableRng;
use tor_bytes::{Error, Reader};
use tor_cell::relaycell::msg::{RelayMsg, Sendme};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

/// A reproducible RNG for encoding cells.
fn rng() -> rand_chacha::ChaCha20Rng {
    rand_chacha::ChaCha20Rng::seed_from_u64(0x0406_0708)
}

/// The wire number of a relay command.
fn relay_num(cmd: RelayCmd) -> u8 {
    cmd.into()
}

#[test]
fn sendme_version__an_empty_sendme_has_no_authentication_tag() {
    let sendme = Sendme::new_empty();
    assert_eq!(sendme.into_tag(), None);
}

#[test]
fn sendme_version__a_version_one_sendme_carries_a_tag() {
    let tag = [0x42_u8; 20];
    let sendme = Sendme::new_tag(tag);
    assert_eq!(sendme.into_tag(), Some(tag.to_vec()));

    // The tag is exactly the twenty bytes it was given, not truncated or
    // padded.
    let some = Sendme::new_tag([7_u8; 20]);
    assert_eq!(some.into_tag().map(|t| t.len()), Some(20));
}

#[test]
fn sendme_version__the_empty_form_round_trips_through_the_wire() {
    let cell = RelayCell::new(
        StreamId::from(0),
        RelayMsg::Sendme(Sendme::new_empty()),
    );
    let body = cell.encode(&mut rng()).expect("cell should encode");
    // An empty SENDME has no body at all in the relay cell.
    let len = u16::from_be_bytes([body[9], body[10]]);
    assert_eq!(len, 0, "an empty SENDME encodes no payload");
    let decoded = RelayCell::decode(body).expect("cell should decode");
    match decoded.msg() {
        RelayMsg::Sendme(sendme) => assert_eq!(sendme.clone().into_tag(), None),
        other => panic!("decoded cell is not a SENDME: {:?}", other.cmd()),
    }
}

#[test]
fn sendme_version__the_version_one_form_round_trips_with_its_tag() {
    let tag = [0x99_u8; 20];
    let cell = RelayCell::new(
        StreamId::from(0),
        RelayMsg::Sendme(Sendme::new_tag(tag)),
    );
    let body = cell.encode(&mut rng()).expect("cell should encode");
    // Version byte, two-byte tag length, twenty tag bytes.
    let len = u16::from_be_bytes([body[9], body[10]]);
    assert_eq!(len, 1 + 2 + 20);
    assert_eq!(body[11], 1, "the version byte is 1");
    assert_eq!(u16::from_be_bytes([body[12], body[13]]), 20);
    assert_eq!(&body[14..34], &tag[..]);

    let decoded = RelayCell::decode(body).expect("cell should decode");
    match decoded.msg() {
        RelayMsg::Sendme(sendme) => assert_eq!(sendme.clone().into_tag(), Some(tag.to_vec())),
        other => panic!("decoded cell is not a SENDME: {:?}", other.cmd()),
    }
}

#[test]
fn sendme_version__an_unrecognized_version_is_refused() {
    // A body with version 2 (unrecognized), a zero tag length, but the command
    // is still SENDME.
    let body = common::relay_body(relay_num(RelayCmd::SENDME), 0, 0, [0_u8; 4], &[2, 0, 0]);
    // The variant is the contract; the wording inside it is the candidate's
    // own diagnostic and is deliberately not asserted.  What must be
    // distinguished is BadMessage (the version is not one we know) from
    // Truncated (the body ran out) and from a successful decode.
    match RelayCell::decode(body) {
        Err(Error::BadMessage(_)) => {}
        other => panic!("expected an unrecognized-version error, got {:?}", other),
    }

    // Version 0 is the "no tag" form: it is a recognized version, so the cell
    // decodes, and it yields no authentication tag whatever follows the
    // version byte.
    let odd = common::relay_body(relay_num(RelayCmd::SENDME), 0, 0, [0_u8; 4], &[0, 0, 1, 9]);
    match RelayCell::decode(odd)
        .expect("version 0 is a recognized SENDME version")
        .msg()
    {
        RelayMsg::Sendme(sendme) => assert_eq!(sendme.clone().into_tag(), None),
        other => panic!("decoded message is not a SENDME: {:?}", other.cmd()),
    }

    // A version-1 SENDME whose declared tag length runs past the end of the
    // relay payload is refused, not silently shortened.
    let short = common::relay_body(
        relay_num(RelayCmd::SENDME),
        0,
        0,
        [0_u8; 4],
        &[1, 0, 20, 0x55, 0x55],
    );
    assert!(
        matches!(RelayCell::decode(short), Err(Error::Truncated)),
        "a tag length past the end of the payload must not decode"
    );

    // The two forms that are recognized still decode: no payload at all, and
    // version 1 with a twenty-byte tag.
    let empty = common::relay_body(relay_num(RelayCmd::SENDME), 0, 0, [0_u8; 4], &[]);
    assert!(RelayCell::decode(empty).is_ok(), "an empty SENDME decodes");

    let mut tagged = vec![1_u8, 0, 20];
    tagged.extend_from_slice(&[0x55_u8; 20]);
    let tagged = common::relay_body(relay_num(RelayCmd::SENDME), 0, 0, [0_u8; 4], &tagged);
    match RelayCell::decode(tagged).expect("a version-1 SENDME decodes").msg() {
        RelayMsg::Sendme(sendme) => assert_eq!(sendme.clone().into_tag(), Some(vec![0x55_u8; 20])),
        other => panic!("decoded message is not a SENDME: {:?}", other.cmd()),
    }

    // The same split, decided on cells captured as files rather than assembled
    // here, so the bytes that carry the version field were not produced by the
    // candidate's own encoder.
    let badversion =
        common::body_from_fixture(include_bytes!("../../fixtures/relay_sendme_badversion.bin"));
    assert_eq!(badversion[0], relay_num(RelayCmd::SENDME));
    assert_eq!(badversion[11], 2, "the captured cell declares version 2");
    match RelayCell::decode(badversion) {
        Err(Error::BadMessage(_)) => {}
        other => panic!("expected an unrecognized-version error, got {:?}", other),
    }

    // ...and the recognized version-1 cell decodes, carrying exactly the twenty
    // tag bytes the file holds, which are 0x30 through 0x43.
    let v1 = common::body_from_fixture(include_bytes!("../../fixtures/relay_sendme_v1.bin"));
    assert_eq!(v1[0], relay_num(RelayCmd::SENDME));
    assert_eq!(v1[11], 1, "the captured cell declares version 1");
    let want: Vec<u8> = (0x30_u8..=0x43).collect();
    assert_eq!(want.len(), 20);
    match RelayCell::decode(v1)
        .expect("a version-1 SENDME from a file decodes")
        .msg()
    {
        RelayMsg::Sendme(sendme) => assert_eq!(sendme.clone().into_tag(), Some(want)),
        other => panic!("decoded message is not a SENDME: {:?}", other.cmd()),
    }
}
