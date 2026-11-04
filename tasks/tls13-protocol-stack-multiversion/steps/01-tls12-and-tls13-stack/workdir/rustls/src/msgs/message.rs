use crate::msgs::alert::AlertMessagePayload;
use crate::msgs::base::Payload;
use crate::msgs::ccs::ChangeCipherSpecPayload;
use crate::msgs::codec::{Codec, Reader};
use crate::msgs::enums::HandshakeType;
use crate::msgs::enums::{AlertDescription, AlertLevel};
use crate::msgs::enums::{ContentType, ProtocolVersion};
use crate::msgs::handshake::HandshakeMessagePayload;

use std::mem;

#[derive(Debug)]
pub enum MessagePayload {
    Alert(AlertMessagePayload),
    Handshake(HandshakeMessagePayload),
    ChangeCipherSpec(ChangeCipherSpecPayload),
    Opaque(Payload),
}

impl MessagePayload {
    pub fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    pub fn decode_given_type(
        &self,
        typ: ContentType,
        vers: ProtocolVersion,
    ) -> Option<MessagePayload> {
        unimplemented!()
    }

    pub fn length(&self) -> usize {
        unimplemented!()
    }

    pub fn new_opaque(data: Vec<u8>) -> MessagePayload {
        unimplemented!()
    }
}

/// A TLS frame, named TLSPlaintext in the standard.
/// This type owns all memory for its interior parts.
#[derive(Debug)]
pub struct Message {
    pub typ: ContentType,
    pub version: ProtocolVersion,
    pub payload: MessagePayload,
}

impl Message {
    /// This is the maximum on-the-wire size of a TLSCiphertext.
    /// That's 2^14 payload bytes, a header, and a 2KB allowance
    /// for ciphertext overheads.
    const MAX_PAYLOAD: u16 = 16384 + 2048;

    /// Content type, version and size.
    const HEADER_SIZE: u16 = 1 + 2 + 2;

    /// Maximum on-wire message size.
    pub const MAX_WIRE_SIZE: usize = (Message::MAX_PAYLOAD + Message::HEADER_SIZE) as usize;
}

impl Codec for Message {
    fn read(r: &mut Reader) -> Option<Message> {
        unimplemented!()
    }

    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }
}

pub enum MessageError {
    TooShortForHeader,
    TooShortForLength,
    IllegalLength,
    IllegalContentType,
    IllegalProtocolVersion,
}

impl Message {
    /// Like Message::read(), but allows the important distinction between:
    /// this message might be valid if we read more data; and this message will
    /// never be valid.
    pub fn read_with_detailed_error(r: &mut Reader) -> Result<Message, MessageError> {
        unimplemented!()
    }

    pub fn is_content_type(&self, typ: ContentType) -> bool {
        unimplemented!()
    }

    pub fn is_handshake_type(&self, hstyp: HandshakeType) -> bool {
        unimplemented!()
    }

    pub fn decode_payload(&mut self) -> bool {
        unimplemented!()
    }

    pub fn take_payload(self) -> Vec<u8> {
        unimplemented!()
    }

    pub fn take_opaque_payload(&mut self) -> Option<Payload> {
        unimplemented!()
    }

    pub fn into_opaque(self) -> Message {
        unimplemented!()
    }

    pub fn build_alert(level: AlertLevel, desc: AlertDescription) -> Message {
        unimplemented!()
    }

    pub fn build_key_update_notify() -> Message {
        unimplemented!()
    }
}

impl<'a> Message {
    pub fn to_borrowed(&'a self) -> BorrowMessage<'a> {
        unimplemented!()
    }
}

/// A TLS frame, named TLSPlaintext in the standard.
///
/// This type differs from `Message` because it borrows
/// its payload.  You can make a `Message` from an
/// `BorrowMessage`, but this involves a copy.
///
/// This type also cannot decode its internals and
/// is not a `Codec` type, only `Message` can do that.
pub struct BorrowMessage<'a> {
    pub typ: ContentType,
    pub version: ProtocolVersion,
    pub payload: &'a [u8],
}
