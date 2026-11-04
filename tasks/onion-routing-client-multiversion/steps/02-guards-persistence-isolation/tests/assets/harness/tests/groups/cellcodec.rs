//! Group `cellcodec` - framing channel cells.
//!
//! Graded through `tor_cell::chancell::codec::ChannelCodec`,
//! `tor_cell::chancell::msg::ChanMsg` and `tor_cell::chancell::{ChanCell,
//! ChanCmd, CircId, CELL_DATA_LEN}`. Every byte a client puts on the wire
//! goes through this encoder, so the two cell formats and the padding that
//! fills a fixed-length cell are graded here, along with the decoder's
//! distinction between "not enough bytes yet" and "these bytes are wrong".

use bytes::BytesMut;
use tor_cell::chancell::codec::ChannelCodec;
use tor_cell::chancell::msg::{ChanMsg, Destroy, DestroyReason, Netinfo, Padding, VPadding};
use tor_cell::chancell::{ChanCell, ChanCmd, CircId, CELL_DATA_LEN};

/// Encode one cell and return the bytes written.
fn encode(codec: &mut ChannelCodec, circid: u32, msg: ChanMsg) -> Vec<u8> {
    let mut buf = BytesMut::new();
    codec
        .write_cell(ChanCell::new(CircId::from(circid), msg), &mut buf)
        .expect("encoding a cell failed");
    buf.to_vec()
}

#[test]
fn cellcodec__a_fixed_length_cell_is_padded_to_the_full_length() {
    let mut codec = ChannelCodec::new(4);
    let bytes = encode(&mut codec, 0x0102_0304, ChanMsg::Destroy(Destroy::new(DestroyReason::NONE)));

    assert_eq!(bytes.len(), CELL_DATA_LEN + 5);
    assert_eq!(CELL_DATA_LEN, 509);
    // Circuit id, then the command byte, then the one-byte reason, then
    // zeros out to the end of the cell.
    assert_eq!(&bytes[..5], &[0x01, 0x02, 0x03, 0x04, 4][..]);
    assert_eq!(bytes[5], 0);
    assert!(
        bytes[6..].iter().all(|b| *b == 0),
        "the fixed-length cell was not zero padded"
    );
}

#[test]
fn cellcodec__a_variable_length_cell_declares_its_own_length() {
    let mut codec = ChannelCodec::new(4);
    // VPADDING is a variable-length cell whose body is a run of zero bytes,
    // so the declared length is the whole story.
    let bytes = encode(&mut codec, 7, ChanMsg::VPadding(VPadding::new(20)));
    assert_eq!(bytes.len(), 7 + 20);
    assert_eq!(&bytes[..5], &[0, 0, 0, 7, 128][..]);
    assert_eq!(&bytes[5..7], &[0, 20][..], "the body length was not written");
    assert!(bytes[7..].iter().all(|b| *b == 0));

    // A zero-length variable cell is legal: seven bytes and nothing else.
    let empty = encode(&mut codec, 7, ChanMsg::VPadding(VPadding::new(0)));
    assert_eq!(empty, vec![0, 0, 0, 7, 128, 0, 0]);

    // A command of 128 or more is variable-length; PADDING is not, even
    // though its body is empty, and so it is padded.
    assert!(ChanCmd::VPADDING.is_var_cell());
    assert!(ChanCmd::VERSIONS.is_var_cell());
    assert!(ChanCmd::CERTS.is_var_cell());
    assert!(!ChanCmd::PADDING.is_var_cell());
    assert!(!ChanCmd::DESTROY.is_var_cell());
    assert!(!ChanCmd::NETINFO.is_var_cell());
    assert_eq!(u8::from(ChanCmd::VPADDING), 128);
    assert_eq!(u8::from(ChanCmd::PADDING), 0);
    assert_eq!(u8::from(ChanCmd::DESTROY), 4);

    let padded = encode(&mut codec, 7, ChanMsg::Padding(Padding::new()));
    assert_eq!(padded.len(), CELL_DATA_LEN + 5);
    assert_eq!(padded[4], 0);
}

#[test]
fn cellcodec__a_whole_decoded_cell_comes_back_out_unchanged() {
    let mut codec = ChannelCodec::new(4);
    let bytes = encode(&mut codec, 0x0000_0009, ChanMsg::Destroy(Destroy::new(DestroyReason::TIMEOUT)));

    let mut buf = BytesMut::from(&bytes[..]);
    let cell = codec
        .decode_cell(&mut buf)
        .expect("decoding failed")
        .expect("a whole cell was reported as incomplete");
    assert!(buf.is_empty(), "the decoder left bytes behind");
    assert_eq!(u32::from(cell.circid()), 9);
    assert_eq!(cell.msg().cmd(), ChanCmd::DESTROY);
    match cell.msg() {
        ChanMsg::Destroy(_) => {}
        other => panic!("a DESTROY command decoded as {:?}", other),
    }

    // A netinfo cell sends real addresses; a client's netinfo names the
    // address it thinks the other side has and no address of its own.
    let netinfo = ChanMsg::Netinfo(Netinfo::for_client(Some("198.51.100.7".parse().unwrap())));
    let bytes = encode(&mut codec, 0, netinfo);
    assert_eq!(bytes.len(), CELL_DATA_LEN + 5);
    let mut buf = BytesMut::from(&bytes[..]);
    let cell = codec
        .decode_cell(&mut buf)
        .expect("decoding a NETINFO failed")
        .expect("a NETINFO cell was reported as incomplete");
    assert_eq!(cell.msg().cmd(), ChanCmd::NETINFO);
    // timestamp(4), one address tag+len+body (1+1+4), then the address count.
    assert_eq!(&bytes[5..15], &[0, 0, 0, 0, 4, 4, 198, 51, 100, 7][..]);
    assert_eq!(bytes[15], 0, "a client reports no addresses of its own");
}

#[test]
fn cellcodec__a_partial_cell_is_incomplete_rather_than_broken() {
    let mut codec = ChannelCodec::new(4);
    let full = encode(&mut codec, 3, ChanMsg::Destroy(Destroy::new(DestroyReason::NONE)));

    // Fewer than seven bytes could not even hold a header.
    for n in 0..7 {
        let mut buf = BytesMut::from(&full[..n]);
        assert!(
            codec
                .decode_cell(&mut buf)
                .unwrap_or_else(|e| panic!("{} bytes was reported as an error: {}", n, e))
                .is_none(),
            "{} bytes was accepted as a whole cell",
            n
        );
        assert_eq!(buf.len(), n, "an incomplete cell was consumed anyway");
    }

    // A fixed-length cell is judged against its full size, not its command.
    let mut buf = BytesMut::from(&full[..full.len() - 1]);
    assert!(codec.decode_cell(&mut buf).expect("no error expected").is_none());

    // A variable-length cell is judged against the length it declares: the
    // header alone says twenty more bytes are coming.
    let mut vp = encode(&mut codec, 1, ChanMsg::VPadding(VPadding::new(20)));
    vp.truncate(7 + 19);
    let mut buf = BytesMut::from(&vp[..]);
    assert!(codec.decode_cell(&mut buf).expect("no error expected").is_none());

    // Two cells in one buffer are decoded one at a time, in order.
    let mut both = encode(&mut codec, 1, ChanMsg::Destroy(Destroy::new(DestroyReason::NONE)));
    both.extend(encode(&mut codec, 2, ChanMsg::Destroy(Destroy::new(DestroyReason::CHANNEL_CLOSED))));
    let mut buf = BytesMut::from(&both[..]);
    let first = codec
        .decode_cell(&mut buf)
        .expect("decoding failed")
        .expect("first cell incomplete");
    assert_eq!(u32::from(first.circid()), 1);
    let second = codec
        .decode_cell(&mut buf)
        .expect("decoding failed")
        .expect("second cell incomplete");
    assert_eq!(u32::from(second.circid()), 2);
    assert!(buf.is_empty());
}

#[test]
fn cellcodec__a_circuit_id_the_command_cannot_use_is_refused() {
    let mut codec = ChannelCodec::new(4);

    // PADDING is a cell about the channel as a whole, so it must carry a
    // zero circuit id; a nonzero one is a protocol error.
    let bytes = encode(&mut codec, 5, ChanMsg::Padding(Padding::new()));
    let mut buf = BytesMut::from(&bytes[..]);
    match codec.decode_cell(&mut buf) {
        Ok(Some(c)) => panic!("a PADDING cell with circuit id 5 was accepted: {:?}", c),
        Ok(None) => panic!("a whole PADDING cell was reported as incomplete"),
        Err(e) => {
            // The wording is a diagnostic and is not graded; the variant is.
            assert!(
                matches!(e, tor_cell::Error::ChanProto(_)),
                "expected a channel protocol violation, got {:?}",
                e
            );
            // It must, however, name the id it refused -- that is data this
            // case supplied, not prose.
            assert!(
                e.to_string().contains('5'),
                "the refusal does not name the offending circuit id: {}",
                e
            );
        }
    }

    // The same cell with a zero circuit id is fine, and the command knows
    // which ids it will accept.
    assert!(ChanCmd::PADDING.accepts_circid_val(CircId::from(0)));
    assert!(!ChanCmd::PADDING.accepts_circid_val(CircId::from(5)));
    assert!(ChanCmd::DESTROY.accepts_circid_val(CircId::from(5)));
    assert!(!ChanCmd::DESTROY.accepts_circid_val(CircId::from(0)));
    assert!(CircId::from(0).is_zero());
    assert!(!CircId::from(1).is_zero());
    assert_eq!(u32::from(CircId::from(0xdead_beef)), 0xdead_beef);
}

#[test]
fn cellcodec__a_circuit_id_of_the_wrong_width_is_not_misread() {
    // The circuit id is four bytes for every cell format this codec
    // supports, so a big one survives the round trip untouched and does not
    // bleed into the command byte.
    let mut codec = ChannelCodec::new(4);
    let bytes = encode(&mut codec, 0xffff_fffe, ChanMsg::Destroy(Destroy::new(DestroyReason::FINISHED)));
    assert_eq!(&bytes[..5], &[0xff, 0xff, 0xff, 0xfe, 4][..]);

    let mut buf = BytesMut::from(&bytes[..]);
    let cell = codec
        .decode_cell(&mut buf)
        .expect("decoding failed")
        .expect("a whole cell was reported as incomplete");
    assert_eq!(u32::from(cell.circid()), 0xffff_fffe);
    assert_eq!(cell.msg().cmd(), ChanCmd::DESTROY);

    // Taking a cell apart gives back exactly what was put in.
    let (circid, msg) = cell.into_circid_and_msg();
    assert_eq!(u32::from(circid), 0xffff_fffe);
    assert_eq!(msg.cmd(), ChanCmd::DESTROY);

    // And an empty buffer is not an error, just nothing to do yet.
    let mut buf = BytesMut::new();
    assert!(codec.decode_cell(&mut buf).expect("no error expected").is_none());
}
