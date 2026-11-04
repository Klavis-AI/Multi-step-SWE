use crate::key;
use crate::msgs::codec;
use crate::msgs::codec::{Codec, Reader};
/// An externally length'd payload
#[derive(Debug, Clone, PartialEq)]
pub struct Payload(pub Vec<u8>);

impl Codec for Payload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<Payload> {
        unimplemented!()
    }
}

impl Payload {
    pub fn new(bytes: Vec<u8>) -> Payload {
        unimplemented!()
    }

    pub fn empty() -> Payload {
        unimplemented!()
    }
}

impl Codec for key::Certificate {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<key::Certificate> {
        unimplemented!()
    }
}

/// An arbitrary, unknown-content, u24-length-prefixed payload
#[derive(Debug, Clone, PartialEq)]
pub struct PayloadU24(pub Vec<u8>);

impl PayloadU24 {
    pub fn new(bytes: Vec<u8>) -> PayloadU24 {
        unimplemented!()
    }
}

impl Codec for PayloadU24 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<PayloadU24> {
        unimplemented!()
    }
}

/// An arbitrary, unknown-content, u16-length-prefixed payload
#[derive(Debug, Clone, PartialEq)]
pub struct PayloadU16(pub Vec<u8>);

impl PayloadU16 {
    pub fn new(bytes: Vec<u8>) -> PayloadU16 {
        unimplemented!()
    }

    pub fn empty() -> PayloadU16 {
        unimplemented!()
    }

    pub fn encode_slice(slice: &[u8], bytes: &mut Vec<u8>) {
        unimplemented!()
    }
}

impl Codec for PayloadU16 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<PayloadU16> {
        unimplemented!()
    }
}

/// An arbitrary, unknown-content, u8-length-prefixed payload
#[derive(Debug, Clone, PartialEq)]
pub struct PayloadU8(pub Vec<u8>);

impl PayloadU8 {
    pub fn new(bytes: Vec<u8>) -> PayloadU8 {
        unimplemented!()
    }

    pub fn empty() -> PayloadU8 {
        unimplemented!()
    }

    pub fn into_inner(self) -> Vec<u8> {
        unimplemented!()
    }
}

impl Codec for PayloadU8 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<PayloadU8> {
        unimplemented!()
    }
}
