use std::fmt::Debug;

/// Read from a byte slice.
pub struct Reader<'a> {
    buf: &'a [u8],
    offs: usize,
}

impl<'a> Reader<'a> {
    pub fn init(bytes: &[u8]) -> Reader {
        unimplemented!()
    }

    pub fn rest(&mut self) -> &[u8] {
        unimplemented!()
    }

    pub fn take(&mut self, len: usize) -> Option<&[u8]> {
        unimplemented!()
    }

    pub fn any_left(&self) -> bool {
        unimplemented!()
    }

    pub fn left(&self) -> usize {
        unimplemented!()
    }

    pub fn used(&self) -> usize {
        unimplemented!()
    }

    pub fn sub(&mut self, len: usize) -> Option<Reader> {
        unimplemented!()
    }
}

/// Things we can encode and read from a Reader.
pub trait Codec: Debug + Sized {
    /// Encode yourself by appending onto `bytes`.
    fn encode(&self, bytes: &mut Vec<u8>);

    /// Decode yourself by fiddling with the `Reader`.
    /// Return Some if it worked, None if not.
    fn read(_: &mut Reader) -> Option<Self>;

    /// Convenience function to get the results of `encode()`.
    fn get_encoding(&self) -> Vec<u8> {
        unimplemented!()
    }

    /// Read one of these from the front of `bytes` and
    /// return it.
    fn read_bytes(bytes: &[u8]) -> Option<Self> {
        unimplemented!()
    }
}

// Encoding functions.
pub fn decode_u8(bytes: &[u8]) -> Option<u8> {
    unimplemented!()
}

impl Codec for u8 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }
    fn read(r: &mut Reader) -> Option<u8> {
        unimplemented!()
    }
}

pub fn put_u16(v: u16, out: &mut [u8]) {
    unimplemented!()
}

pub fn decode_u16(bytes: &[u8]) -> Option<u16> {
    unimplemented!()
}

impl Codec for u16 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<u16> {
        unimplemented!()
    }
}

// Make a distinct type for u24, even though it's a u32 underneath
#[allow(non_camel_case_types)]
#[derive(Debug)]
pub struct u24(pub u32);

impl u24 {
    pub fn decode(bytes: &[u8]) -> Option<u24> {
        unimplemented!()
    }
}

impl Codec for u24 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<u24> {
        unimplemented!()
    }
}

pub fn decode_u32(bytes: &[u8]) -> Option<u32> {
    unimplemented!()
}

impl Codec for u32 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<u32> {
        unimplemented!()
    }
}

pub fn put_u64(v: u64, bytes: &mut [u8]) {
    unimplemented!()
}

pub fn decode_u64(bytes: &[u8]) -> Option<u64> {
    unimplemented!()
}

impl Codec for u64 {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<u64> {
        unimplemented!()
    }
}

pub fn encode_vec_u8<T: Codec>(bytes: &mut Vec<u8>, items: &[T]) {
    unimplemented!()
}

pub fn encode_vec_u16<T: Codec>(bytes: &mut Vec<u8>, items: &[T]) {
    unimplemented!()
}

pub fn encode_vec_u24<T: Codec>(bytes: &mut Vec<u8>, items: &[T]) {
    unimplemented!()
}

pub fn read_vec_u8<T: Codec>(r: &mut Reader) -> Option<Vec<T>> {
    unimplemented!()
}

pub fn read_vec_u16<T: Codec>(r: &mut Reader) -> Option<Vec<T>> {
    unimplemented!()
}

pub fn read_vec_u24_limited<T: Codec>(r: &mut Reader, max_bytes: usize) -> Option<Vec<T>> {
    unimplemented!()
}
