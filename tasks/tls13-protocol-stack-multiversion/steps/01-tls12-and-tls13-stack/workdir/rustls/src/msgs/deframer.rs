use std::collections::VecDeque;
use std::io;

use crate::msgs::codec;
use crate::msgs::message::{Message, MessageError};

/// This deframer works to reconstruct TLS messages
/// from arbitrary-sized reads, buffering as necessary.
/// The input is `read()`, the output is the `frames` deque.
pub struct MessageDeframer {
    /// Completed frames for output.
    pub frames: VecDeque<Message>,

    /// Set to true if the peer is not talking TLS, but some other
    /// protocol.  The caller should abort the connection, because
    /// the deframer cannot recover.
    pub desynced: bool,

    /// A fixed-size buffer containing the currently-accumulating
    /// TLS message.
    buf: Box<[u8; Message::MAX_WIRE_SIZE]>,

    /// What size prefix of `buf` is used.
    used: usize,
}

enum BufferContents {
    /// Contains an invalid message as a header.
    Invalid,

    /// Might contain a valid message if we receive more.
    /// Perhaps totally empty!
    Partial,

    /// Contains a valid frame as a prefix.
    Valid,
}

impl Default for MessageDeframer {
    fn default() -> Self {
        unimplemented!()
    }
}

impl MessageDeframer {
    pub fn new() -> MessageDeframer {
        unimplemented!()
    }

    /// Read some bytes from `rd`, and add them to our internal
    /// buffer.  If this means our internal buffer contains
    /// full messages, decode them all.
    pub fn read(&mut self, rd: &mut dyn io::Read) -> io::Result<usize> {
        unimplemented!()
    }

    /// Returns true if we have messages for the caller
    /// to process, either whole messages in our output
    /// queue or partial messages in our buffer.
    pub fn has_pending(&self) -> bool {
        unimplemented!()
    }

    /// Does our `buf` contain a full message?  It does if it is big enough to
    /// contain a header, and that header has a length which falls within `buf`.
    /// If so, deframe it and place the message onto the frames output queue.
    fn try_deframe_one(&mut self) -> BufferContents {
        unimplemented!()
    }

    fn buf_consume(&mut self, taken: usize) {
        unimplemented!()
    }
}

#[cfg(test)]
mod tests {
    use super::MessageDeframer;
    use crate::msgs;
    use std::io;

    const FIRST_MESSAGE: &'static [u8] = include_bytes!("../testdata/deframer-test.1.bin");
    const SECOND_MESSAGE: &'static [u8] = include_bytes!("../testdata/deframer-test.2.bin");

    struct ByteRead<'a> {
        buf: &'a [u8],
        offs: usize,
    }

    impl<'a> ByteRead<'a> {
        fn new(bytes: &'a [u8]) -> ByteRead {
            unimplemented!()
        }
    }

    impl<'a> io::Read for ByteRead<'a> {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            unimplemented!()
        }
    }

    fn input_bytes(d: &mut MessageDeframer, bytes: &[u8]) -> io::Result<usize> {
        unimplemented!()
    }

    fn input_bytes_concat(
        d: &mut MessageDeframer,
        bytes1: &[u8],
        bytes2: &[u8],
    ) -> io::Result<usize> {
        unimplemented!()
    }

    struct ErrorRead {
        error: Option<io::Error>,
    }

    impl ErrorRead {
        fn new(error: io::Error) -> ErrorRead {
            unimplemented!()
        }
    }

    impl io::Read for ErrorRead {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            unimplemented!()
        }
    }

    fn input_error(d: &mut MessageDeframer) {
        unimplemented!()
    }

    fn input_whole_incremental(d: &mut MessageDeframer, bytes: &[u8]) {
        unimplemented!()
    }

    fn assert_len(want: usize, got: io::Result<usize>) {
        unimplemented!()
    }

    fn pop_first(d: &mut MessageDeframer) {
        unimplemented!()
    }

    fn pop_second(d: &mut MessageDeframer) {
        unimplemented!()
    }

    #[test]
    fn check_incremental() {
        unimplemented!()
    }

    #[test]
    fn check_incremental_2() {
        unimplemented!()
    }

    #[test]
    fn check_whole() {
        unimplemented!()
    }

    #[test]
    fn check_whole_2() {
        unimplemented!()
    }

    #[test]
    fn test_two_in_one_read() {
        unimplemented!()
    }

    #[test]
    fn test_two_in_one_read_shortest_first() {
        unimplemented!()
    }

    #[test]
    fn test_incremental_with_nonfatal_read_error() {
        unimplemented!()
    }
}
