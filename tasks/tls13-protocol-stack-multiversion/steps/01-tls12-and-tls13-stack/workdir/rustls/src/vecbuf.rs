use std::cmp;
use std::collections::VecDeque;
use std::io;
use std::io::Read;

/// This is a byte buffer that is built from a vector
/// of byte vectors.  This avoids extra copies when
/// appending a new byte vector, at the expense of
/// more complexity when reading out.
pub struct ChunkVecBuffer {
    chunks: VecDeque<Vec<u8>>,
    limit: usize,
}

impl ChunkVecBuffer {
    pub fn new() -> ChunkVecBuffer {
        unimplemented!()
    }

    /// Sets the upper limit on how many bytes this
    /// object can store.
    ///
    /// Setting a lower limit than the currently stored
    /// data is not an error.
    ///
    /// A zero limit is interpreted as no limit.
    pub fn set_limit(&mut self, new_limit: usize) {
        unimplemented!()
    }

    /// If we're empty
    pub fn is_empty(&self) -> bool {
        unimplemented!()
    }

    /// How many bytes we're storing
    pub fn len(&self) -> usize {
        unimplemented!()
    }

    /// For a proposed append of `len` bytes, how many
    /// bytes should we actually append to adhere to the
    /// currently set `limit`?
    pub fn apply_limit(&self, len: usize) -> usize {
        unimplemented!()
    }

    /// Append a copy of `bytes`, perhaps a prefix if
    /// we're near the limit.
    pub fn append_limited_copy(&mut self, bytes: &[u8]) -> usize {
        unimplemented!()
    }

    /// Take and append the given `bytes`.
    pub fn append(&mut self, bytes: Vec<u8>) -> usize {
        unimplemented!()
    }

    /// Take one of the chunks from this object.  This
    /// function panics if the object `is_empty`.
    pub fn take_one(&mut self) -> Vec<u8> {
        unimplemented!()
    }

    /// Read data out of this object, writing it into `buf`
    /// and returning how many bytes were written there.
    pub fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        unimplemented!()
    }

    fn consume(&mut self, mut used: usize) {
        unimplemented!()
    }

    /// Read data out of this object, passing it `wr`
    pub fn write_to(&mut self, wr: &mut dyn io::Write) -> io::Result<usize> {
        unimplemented!()
    }
}

#[cfg(test)]
mod test {
    use super::ChunkVecBuffer;

    #[test]
    fn short_append_copy_with_limit() {
        unimplemented!()
    }
}
