use std::collections::VecDeque;

use crate::msgs::codec;
use crate::msgs::enums::{ContentType, ProtocolVersion};
use crate::msgs::handshake::HandshakeMessagePayload;
use crate::msgs::message::{Message, MessagePayload};

const HEADER_SIZE: usize = 1 + 3;

/// This works to reconstruct TLS handshake messages
/// from individual TLS messages.  It's guaranteed that
/// TLS messages output from this layer contain precisely
/// one handshake payload.
pub struct HandshakeJoiner {
    /// Completed handshake frames for output.
    pub frames: VecDeque<Message>,

    /// The message payload we're currently accumulating.
    buf: Vec<u8>,
}

impl Default for HandshakeJoiner {
    fn default() -> Self {
        unimplemented!()
    }
}

impl HandshakeJoiner {
    /// Make a new HandshakeJoiner.
    pub fn new() -> HandshakeJoiner {
        unimplemented!()
    }

    /// Do we want to process this message?
    pub fn want_message(&self, msg: &Message) -> bool {
        unimplemented!()
    }

    /// Do we have any buffered data?
    pub fn is_empty(&self) -> bool {
        unimplemented!()
    }

    /// Take the message, and join/split it as needed.
    /// Return the number of new messages added to the
    /// output deque as a result of this message.
    ///
    /// Returns None if msg or a preceding message was corrupt.
    /// You cannot recover from this situation.  Otherwise returns
    /// a count of how many messages we queued.
    pub fn take_message(&mut self, mut msg: Message) -> Option<usize> {
        unimplemented!()
    }

    /// Does our `buf` contain a full handshake payload?  It does if it is big
    /// enough to contain a header, and that header has a length which falls
    /// within `buf`.
    fn buf_contains_message(&self) -> bool {
        unimplemented!()
    }

    /// Take a TLS handshake payload off the front of `buf`, and put it onto
    /// the back of our `frames` deque inside a normal `Message`.
    ///
    /// Returns false if the stream is desynchronised beyond repair.
    fn deframe_one(&mut self, version: ProtocolVersion) -> bool {
        unimplemented!()
    }
}

#[cfg(test)]
mod tests {
    use super::HandshakeJoiner;
    use crate::msgs::base::Payload;
    use crate::msgs::enums::{ContentType, HandshakeType, ProtocolVersion};
    use crate::msgs::handshake::{HandshakeMessagePayload, HandshakePayload};
    use crate::msgs::message::{Message, MessagePayload};

    #[test]
    fn want() {
        unimplemented!()
    }

    fn pop_eq(expect: &Message, hj: &mut HandshakeJoiner) {
        unimplemented!()
    }

    #[test]
    fn split() {
        unimplemented!()
    }

    #[test]
    fn broken() {
        unimplemented!()
    }

    #[test]
    fn join() {
        unimplemented!()
    }

    #[test]
    fn test_rejoins_then_rejects_giant_certs() {
        unimplemented!()
    }
}
