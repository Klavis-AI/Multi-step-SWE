use crate::msgs::enums::{ContentType, ProtocolVersion};
use crate::msgs::message::{BorrowMessage, Message, MessagePayload};
use std::collections::VecDeque;

pub const MAX_FRAGMENT_LEN: usize = 16384;
pub const PACKET_OVERHEAD: usize = 1 + 2 + 2;

pub struct MessageFragmenter {
    max_frag: usize,
}

impl MessageFragmenter {
    /// Make a new fragmenter.  `max_fragment_len` is the maximum
    /// fragment size that will be produced -- this does not
    /// include overhead (so a `max_fragment_len` of 5 will produce
    /// 10 byte packets).
    pub fn new(max_fragment_len: usize) -> MessageFragmenter {
        unimplemented!()
    }

    /// Take the Message `msg` and re-fragment it into new
    /// messages whose fragment is no more than max_frag.
    /// The new messages are appended to the `out` deque.
    /// Payloads are copied.
    pub fn fragment(&self, msg: Message, out: &mut VecDeque<Message>) {
        unimplemented!()
    }

    /// Enqueue borrowed fragments of (version, typ, payload) which
    /// are no longer than max_frag onto the `out` deque.
    pub fn fragment_borrow<'a>(
        &self,
        typ: ContentType,
        version: ProtocolVersion,
        payload: &'a [u8],
        out: &mut VecDeque<BorrowMessage<'a>>,
    ) {
        unimplemented!()
    }
}

#[cfg(test)]
mod tests {
    use super::{MessageFragmenter, PACKET_OVERHEAD};
    use crate::msgs::codec::Codec;
    use crate::msgs::enums::{ContentType, ProtocolVersion};
    use crate::msgs::message::{Message, MessagePayload};
    use std::collections::VecDeque;

    fn msg_eq(
        mm: Option<Message>,
        total_len: usize,
        typ: &ContentType,
        version: &ProtocolVersion,
        bytes: &[u8],
    ) {
        unimplemented!()
    }

    #[test]
    fn smoke() {
        unimplemented!()
    }

    #[test]
    fn non_fragment() {
        unimplemented!()
    }
}
