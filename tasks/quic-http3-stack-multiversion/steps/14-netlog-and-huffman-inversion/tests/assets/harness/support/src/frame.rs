//! Just enough frame decoding to inspect a decrypted packet payload.
//!
//! The scope started as the set of frames RFC 9000 Section 17.2.2 allows in an
//! Initial packet — PADDING, PING, ACK, CRYPTO and CONNECTION_CLOSE — and has
//! grown with the steps that needed to read more: the loss-recovery groups
//! read 1-RTT payloads, so RESET_STREAM, STOP_SENDING, the STREAM range,
//! MAX_DATA/MAX_STREAM_DATA/MAX_STREAMS and the blocked-frame family are
//! decoded too, as are NEW_TOKEN, NEW_CONNECTION_ID, RETIRE_CONNECTION_ID,
//! PATH_CHALLENGE, PATH_RESPONSE, HANDSHAKE_DONE and the DATAGRAM pair.
//! Anything still unrecognised stops iteration with [`Error::UnknownFrame`]
//! rather than being skipped: a frame's length is implied by its type, so
//! there is no way to step over one without inventing bytes.

use std::collections::BTreeMap;

use crate::varint::Reader;
use crate::{Error, Result};

const FRAME_PADDING: u64 = 0x00;
const FRAME_PING: u64 = 0x01;
const FRAME_ACK: u64 = 0x02;
const FRAME_ACK_ECN: u64 = 0x03;
const FRAME_CRYPTO: u64 = 0x06;
const FRAME_CONNECTION_CLOSE_TRANSPORT: u64 = 0x1c;
const FRAME_CONNECTION_CLOSE_APPLICATION: u64 = 0x1d;
const FRAME_RESET_STREAM: u64 = 0x04;
const FRAME_STOP_SENDING: u64 = 0x05;
const FRAME_NEW_TOKEN: u64 = 0x07;
const FRAME_STREAM_BASE: u64 = 0x08;
const FRAME_STREAM_MAX: u64 = 0x0f;
const FRAME_STREAM_FIN_BIT: u64 = 0x01;
const FRAME_STREAM_LEN_BIT: u64 = 0x02;
const FRAME_STREAM_OFF_BIT: u64 = 0x04;
const FRAME_MAX_DATA: u64 = 0x10;
const FRAME_MAX_STREAM_DATA: u64 = 0x11;
const FRAME_MAX_STREAMS_BIDI: u64 = 0x12;
const FRAME_MAX_STREAMS_UNI: u64 = 0x13;
const FRAME_DATA_BLOCKED: u64 = 0x14;
const FRAME_STREAM_DATA_BLOCKED: u64 = 0x15;
const FRAME_STREAMS_BLOCKED_BIDI: u64 = 0x16;
const FRAME_STREAMS_BLOCKED_UNI: u64 = 0x17;
const FRAME_NEW_CONNECTION_ID: u64 = 0x18;
const FRAME_RETIRE_CONNECTION_ID: u64 = 0x19;
const FRAME_PATH_CHALLENGE: u64 = 0x1a;
const FRAME_PATH_RESPONSE: u64 = 0x1b;
const FRAME_HANDSHAKE_DONE: u64 = 0x1e;
const FRAME_DATAGRAM: u64 = 0x30;
const FRAME_DATAGRAM_LEN: u64 = 0x31;

/// Length of the Stateless Reset Token carried by NEW_CONNECTION_ID.
const RESET_TOKEN_LEN: usize = 16;

/// Length of the opaque data carried by PATH_CHALLENGE and PATH_RESPONSE.
const PATH_DATA_LEN: usize = 8;

/// ECN counts carried by an ACK frame of type 0x03.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EcnCounts {
    /// Packets received with the ECT(0) codepoint.
    pub ect0: u64,
    /// Packets received with the ECT(1) codepoint.
    pub ect1: u64,
    /// Packets received with the ECN-CE codepoint.
    pub ce: u64,
}

/// A decoded frame. Payload slices borrow from the decrypted packet payload.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Frame<'a> {
    /// One or more consecutive PADDING frames, coalesced.
    Padding {
        /// How many zero bytes were consumed.
        len: usize,
    },
    /// PING.
    Ping,
    /// ACK, with its ranges already expanded.
    Ack {
        /// Largest packet number acknowledged.
        largest_acked: u64,
        /// Encoded ack delay, in units of the peer's `ack_delay_exponent`.
        ack_delay: u64,
        /// Acknowledged packet number ranges as inclusive `(smallest,
        /// largest)` pairs, largest range first — the order they appear on
        /// the wire.
        ranges: Vec<(u64, u64)>,
        /// ECN counts, present only for the 0x03 form.
        ecn: Option<EcnCounts>,
    },
    /// CRYPTO: a chunk of the TLS handshake stream at a given offset.
    Crypto {
        /// Offset of this chunk within the crypto stream.
        offset: u64,
        /// The chunk.
        data: &'a [u8],
    },
    /// CONNECTION_CLOSE, either the transport (0x1c) or application (0x1d) form.
    ConnectionClose {
        /// The error code. Transport error codes are RFC 9000 Section 20.1;
        /// application error codes are defined by the application protocol,
        /// e.g. HTTP/3 in RFC 9114 Section 8.1.
        error_code: u64,
        /// The frame type that triggered the close, present only in the
        /// transport form. `Some(0)` means "unknown".
        frame_type: Option<u64>,
        /// Human-readable reason phrase, possibly empty and not necessarily
        /// valid UTF-8.
        reason: &'a [u8],
    },
    /// RESET_STREAM: the sender has abandoned a stream's send side.
    ResetStream {
        /// The stream being reset.
        stream_id: u64,
        /// Application error code.
        error_code: u64,
        /// Final size of the stream, in bytes.
        final_size: u64,
    },
    /// STOP_SENDING: the receiver wants no more data on a stream.
    StopSending {
        /// The stream the peer should stop sending on.
        stream_id: u64,
        /// Application error code.
        error_code: u64,
    },
    /// NEW_TOKEN: a token the client may present on a future connection.
    NewToken {
        /// The opaque token.
        token: &'a [u8],
    },
    /// STREAM: application data on a stream.
    Stream {
        /// The stream this data belongs to.
        stream_id: u64,
        /// Byte offset of `data` within the stream. Zero when the frame
        /// carried no OFF bit.
        offset: u64,
        /// Whether the FIN bit was set, ending the stream at
        /// `offset + data.len()`.
        fin: bool,
        /// The stream data. When the LEN bit was absent this runs to the end
        /// of the packet payload.
        data: &'a [u8],
    },
    /// MAX_DATA: a new connection-level flow control limit.
    MaxData {
        /// The cumulative limit, in bytes, across all streams.
        max: u64,
    },
    /// MAX_STREAM_DATA: a new stream-level flow control limit.
    MaxStreamData {
        /// The stream the limit applies to.
        stream_id: u64,
        /// The cumulative limit, in bytes, on that stream.
        max: u64,
    },
    /// MAX_STREAMS: a new limit on how many streams may be opened.
    MaxStreams {
        /// True for the bidirectional limit (0x12), false for the
        /// unidirectional one (0x13).
        bidi: bool,
        /// The cumulative count of streams permitted.
        max: u64,
    },
    /// DATA_BLOCKED: the sender has data to send but is at the connection limit.
    DataBlocked {
        /// The connection-level limit at which the sender is blocked.
        limit: u64,
    },
    /// STREAM_DATA_BLOCKED: blocked at a stream-level limit.
    StreamDataBlocked {
        /// The stream on which the sender is blocked.
        stream_id: u64,
        /// The stream-level limit at which it is blocked.
        limit: u64,
    },
    /// STREAMS_BLOCKED: the sender wants to open a stream but is at the limit.
    StreamsBlocked {
        /// True for the bidirectional limit (0x16), false for the
        /// unidirectional one (0x17).
        bidi: bool,
        /// The stream-count limit at which the sender is blocked.
        limit: u64,
    },
    /// NEW_CONNECTION_ID: an additional connection ID the peer may use.
    NewConnectionId {
        /// Sequence number of this connection ID.
        seq: u64,
        /// The peer should retire every connection ID below this sequence
        /// number.
        retire_prior_to: u64,
        /// The connection ID itself.
        cid: &'a [u8],
        /// The 16-byte stateless reset token for this connection ID.
        reset_token: &'a [u8],
    },
    /// RETIRE_CONNECTION_ID: the sender will no longer use a connection ID.
    RetireConnectionId {
        /// Sequence number of the retired connection ID.
        seq: u64,
    },
    /// PATH_CHALLENGE: path validation probe.
    PathChallenge {
        /// Eight opaque bytes the peer must echo in a PATH_RESPONSE.
        data: &'a [u8],
    },
    /// PATH_RESPONSE: the echo of a PATH_CHALLENGE.
    PathResponse {
        /// The eight bytes copied from the challenge.
        data: &'a [u8],
    },
    /// HANDSHAKE_DONE: the server confirms the handshake is complete.
    HandshakeDone,
    /// DATAGRAM: unreliable application data, outside the stream layer.
    ///
    /// Defined by draft-ietf-quic-datagram and later RFC 9221. A DATAGRAM
    /// frame is ack-eliciting but is never retransmitted when lost, which is
    /// the property that distinguishes it from a STREAM frame carrying the
    /// same bytes.
    Datagram {
        /// The datagram payload.
        data: &'a [u8],
        /// True for the 0x31 form, which carries an explicit varint length;
        /// false for the 0x30 form, whose payload runs to the end of the
        /// packet and which therefore must be the last frame.
        has_len: bool,
    },
}

impl Frame<'_> {
    /// Whether this frame is a transport-level CONNECTION_CLOSE, as opposed to
    /// an application one. Useful because the error code spaces differ.
    pub fn is_transport_close(&self) -> bool {
        matches!(self, Frame::ConnectionClose { frame_type: Some(_), .. })
    }
}

/// Iterator over the frames of a decrypted payload.
///
/// Yields `Err` at most once: after any error the iterator is fused, because
/// resynchronising on a payload that already failed to parse would only
/// produce fiction.
#[derive(Clone, Debug)]
pub struct Frames<'a> {
    r: Reader<'a>,
    stopped: bool,
}

/// Iterate the frames of a decrypted packet payload.
pub fn frames(payload: &[u8]) -> Frames<'_> {
    Frames { r: Reader::new(payload), stopped: false }
}

/// Decode every frame in a payload, failing on the first problem.
pub fn parse_frames(payload: &[u8]) -> Result<Vec<Frame<'_>>> {
    frames(payload).collect()
}

impl<'a> Iterator for Frames<'a> {
    type Item = Result<Frame<'a>>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.stopped || self.r.is_empty() {
            return None;
        }

        match self.parse_one() {
            Ok(frame) => Some(Ok(frame)),
            Err(e) => {
                self.stopped = true;
                Some(Err(e))
            },
        }
    }
}

impl<'a> Frames<'a> {
    fn parse_one(&mut self) -> Result<Frame<'a>> {
        let ty = self.r.varint("frame type")?;

        match ty {
            FRAME_PADDING => {
                // PADDING is a single zero byte, but a packet padded to 1200
                // bytes holds hundreds of them; report the run.
                let mut len = 1;
                while !self.r.is_empty() {
                    let mut probe = self.r.clone();
                    if probe.u8("padding")? != 0 {
                        break;
                    }
                    self.r = probe;
                    len += 1;
                }
                Ok(Frame::Padding { len })
            },

            FRAME_PING => Ok(Frame::Ping),

            FRAME_ACK | FRAME_ACK_ECN => {
                let largest_acked = self.r.varint("ack largest acknowledged")?;
                let ack_delay = self.r.varint("ack delay")?;
                let range_count = self.r.varint("ack range count")?;
                let first_range = self.r.varint("first ack range")?;

                let mut ranges = Vec::new();
                let mut smallest = largest_acked
                    .checked_sub(first_range)
                    .ok_or(Error::Invalid("first ack range (underflows)"))?;
                ranges.push((smallest, largest_acked));

                for _ in 0..range_count {
                    let gap = self.r.varint("ack range gap")?;
                    let len = self.r.varint("ack range length")?;

                    // RFC 9000 Section 19.3.1: the next largest is
                    // smallest - gap - 2.
                    let largest = smallest
                        .checked_sub(gap)
                        .and_then(|v| v.checked_sub(2))
                        .ok_or(Error::Invalid("ack range gap (underflows)"))?;
                    smallest = largest
                        .checked_sub(len)
                        .ok_or(Error::Invalid("ack range length (underflows)"))?;
                    ranges.push((smallest, largest));
                }

                let ecn = if ty == FRAME_ACK_ECN {
                    Some(EcnCounts {
                        ect0: self.r.varint("ECT(0) count")?,
                        ect1: self.r.varint("ECT(1) count")?,
                        ce: self.r.varint("ECN-CE count")?,
                    })
                } else {
                    None
                };

                Ok(Frame::Ack { largest_acked, ack_delay, ranges, ecn })
            },

            FRAME_CRYPTO => {
                let offset = self.r.varint("crypto offset")?;
                let data = self.r.varint_prefixed("crypto data")?;
                Ok(Frame::Crypto { offset, data })
            },

            FRAME_CONNECTION_CLOSE_TRANSPORT | FRAME_CONNECTION_CLOSE_APPLICATION => {
                let error_code = self.r.varint("connection close error code")?;
                let frame_type = if ty == FRAME_CONNECTION_CLOSE_TRANSPORT {
                    Some(self.r.varint("connection close frame type")?)
                } else {
                    None
                };
                let reason = self.r.varint_prefixed("connection close reason")?;
                Ok(Frame::ConnectionClose { error_code, frame_type, reason })
            },

            FRAME_RESET_STREAM => {
                let stream_id = self.r.varint("reset stream id")?;
                let error_code = self.r.varint("reset stream error code")?;
                let final_size = self.r.varint("reset stream final size")?;
                Ok(Frame::ResetStream { stream_id, error_code, final_size })
            },

            FRAME_STOP_SENDING => {
                let stream_id = self.r.varint("stop sending stream id")?;
                let error_code = self.r.varint("stop sending error code")?;
                Ok(Frame::StopSending { stream_id, error_code })
            },

            FRAME_NEW_TOKEN => {
                let token = self.r.varint_prefixed("new token")?;
                Ok(Frame::NewToken { token })
            },

            ty if (FRAME_STREAM_BASE..=FRAME_STREAM_MAX).contains(&ty) => {
                // RFC 9000 Section 19.8: the low three bits of the type are
                // OFF, LEN and FIN. A frame with no LEN bit runs to the end of
                // the payload, so it can only be the last frame in a packet.
                let stream_id = self.r.varint("stream id")?;

                let offset = if ty & FRAME_STREAM_OFF_BIT != 0 {
                    self.r.varint("stream offset")?
                } else {
                    0
                };

                let data = if ty & FRAME_STREAM_LEN_BIT != 0 {
                    self.r.varint_prefixed("stream data")?
                } else {
                    self.r.rest()
                };

                let fin = ty & FRAME_STREAM_FIN_BIT != 0;
                Ok(Frame::Stream { stream_id, offset, fin, data })
            },

            FRAME_MAX_DATA => {
                let max = self.r.varint("max data")?;
                Ok(Frame::MaxData { max })
            },

            FRAME_MAX_STREAM_DATA => {
                let stream_id = self.r.varint("max stream data stream id")?;
                let max = self.r.varint("max stream data")?;
                Ok(Frame::MaxStreamData { stream_id, max })
            },

            FRAME_MAX_STREAMS_BIDI | FRAME_MAX_STREAMS_UNI => {
                let max = self.r.varint("max streams")?;
                Ok(Frame::MaxStreams { bidi: ty == FRAME_MAX_STREAMS_BIDI, max })
            },

            FRAME_DATA_BLOCKED => {
                let limit = self.r.varint("data blocked limit")?;
                Ok(Frame::DataBlocked { limit })
            },

            FRAME_STREAM_DATA_BLOCKED => {
                let stream_id = self.r.varint("stream data blocked stream id")?;
                let limit = self.r.varint("stream data blocked limit")?;
                Ok(Frame::StreamDataBlocked { stream_id, limit })
            },

            FRAME_STREAMS_BLOCKED_BIDI | FRAME_STREAMS_BLOCKED_UNI => {
                let limit = self.r.varint("streams blocked limit")?;
                Ok(Frame::StreamsBlocked { bidi: ty == FRAME_STREAMS_BLOCKED_BIDI, limit })
            },

            FRAME_NEW_CONNECTION_ID => {
                let seq = self.r.varint("new connection id sequence number")?;
                let retire_prior_to = self.r.varint("new connection id retire prior to")?;
                let cid_len = self.r.u8("new connection id length")? as usize;
                if cid_len > crate::packet::MAX_CID_LEN {
                    return Err(Error::Invalid("new connection id length"));
                }
                let cid = self.r.bytes(cid_len, "new connection id")?;
                let reset_token = self.r.bytes(RESET_TOKEN_LEN, "stateless reset token")?;
                Ok(Frame::NewConnectionId { seq, retire_prior_to, cid, reset_token })
            },

            FRAME_RETIRE_CONNECTION_ID => {
                let seq = self.r.varint("retire connection id sequence number")?;
                Ok(Frame::RetireConnectionId { seq })
            },

            FRAME_PATH_CHALLENGE => {
                let data = self.r.bytes(PATH_DATA_LEN, "path challenge data")?;
                Ok(Frame::PathChallenge { data })
            },

            FRAME_PATH_RESPONSE => {
                let data = self.r.bytes(PATH_DATA_LEN, "path response data")?;
                Ok(Frame::PathResponse { data })
            },

            FRAME_HANDSHAKE_DONE => Ok(Frame::HandshakeDone),

            FRAME_DATAGRAM | FRAME_DATAGRAM_LEN => {
                // draft-ietf-quic-datagram Section 4 / RFC 9221 Section 4:
                // the 0x30 form has no length and runs to the end of the
                // packet; the 0x31 form carries an explicit varint length.
                let has_len = ty == FRAME_DATAGRAM_LEN;
                let data = if has_len {
                    self.r.varint_prefixed("datagram data")?
                } else {
                    self.r.rest()
                };
                Ok(Frame::Datagram { data, has_len })
            },

            other => Err(Error::UnknownFrame(other)),
        }
    }
}

/// Reassembles the CRYPTO stream out of CRYPTO frames.
///
/// The TLS handshake is a byte stream spread across frames that may arrive out
/// of order, be split at arbitrary points, overlap, or be duplicated across
/// retransmissions. Feed payloads in as they are decrypted and ask for the
/// contiguous prefix; a ClientHello spanning several packets only becomes
/// parseable once the gap in the middle is filled.
#[derive(Clone, Debug, Default)]
pub struct CryptoAssembler {
    chunks: BTreeMap<u64, Vec<u8>>,
}

impl CryptoAssembler {
    /// A new, empty assembler.
    pub fn new() -> Self {
        Self::default()
    }

    /// Add one CRYPTO frame's data.
    pub fn push(&mut self, offset: u64, data: &[u8]) {
        if data.is_empty() {
            return;
        }
        // Keep the longer of two chunks at the same offset; overlap elsewhere
        // is resolved when assembling.
        self.chunks
            .entry(offset)
            .and_modify(|existing| {
                if existing.len() < data.len() {
                    *existing = data.to_vec();
                }
            })
            .or_insert_with(|| data.to_vec());
    }

    /// Add every CRYPTO frame in a decrypted payload, ignoring the rest.
    pub fn absorb(&mut self, payload: &[u8]) -> Result<()> {
        for frame in frames(payload) {
            if let Frame::Crypto { offset, data } = frame? {
                self.push(offset, data);
            }
        }
        Ok(())
    }

    /// The contiguous byte stream starting at offset 0, stopping at the first
    /// gap.
    pub fn contiguous(&self) -> Vec<u8> {
        let mut out: Vec<u8> = Vec::new();

        for (&offset, data) in &self.chunks {
            let end = offset + data.len() as u64;
            let have = out.len() as u64;

            if offset > have {
                // A gap: everything beyond it stays buffered.
                break;
            }
            if end <= have {
                // Wholly duplicated by what is already assembled.
                continue;
            }

            let skip = (have - offset) as usize;
            out.extend_from_slice(&data[skip..]);
        }

        out
    }

    /// Whether no CRYPTO data has been seen at all.
    pub fn is_empty(&self) -> bool {
        self.chunks.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn padding_runs_are_coalesced() {
        let payload = [0x01, 0x00, 0x00, 0x00, 0x01];
        let got = parse_frames(&payload).unwrap();
        assert_eq!(got, vec![Frame::Ping, Frame::Padding { len: 3 }, Frame::Ping]);
    }

    #[test]
    fn crypto_frame_round_trip() {
        let payload = [0x06, 0x00, 0x03, 0xaa, 0xbb, 0xcc];
        assert_eq!(
            parse_frames(&payload).unwrap(),
            vec![Frame::Crypto { offset: 0, data: &[0xaa, 0xbb, 0xcc] }]
        );
    }

    #[test]
    fn connection_close_carries_the_error_code() {
        // Transport close, error 0x0a (PROTOCOL_VIOLATION is 0x0a), triggered
        // by frame type 0x06, reason "no".
        let payload = [0x1c, 0x0a, 0x06, 0x02, b'n', b'o'];
        assert_eq!(
            parse_frames(&payload).unwrap(),
            vec![Frame::ConnectionClose {
                error_code: 0x0a,
                frame_type: Some(0x06),
                reason: b"no"
            }]
        );

        // Application close has no frame type field.
        let payload = [0x1d, 0x41, 0x04, 0x00];
        let frames = parse_frames(&payload).unwrap();
        assert_eq!(
            frames,
            vec![Frame::ConnectionClose { error_code: 260, frame_type: None, reason: b"" }]
        );
        assert!(!frames[0].is_transport_close());
    }

    #[test]
    fn ack_ranges_are_expanded() {
        // largest 10, delay 0, 1 extra range, first range 2 => 8..=10,
        // then gap 0, length 1 => 5..=6.
        let payload = [0x02, 0x0a, 0x00, 0x01, 0x02, 0x00, 0x01];
        assert_eq!(
            parse_frames(&payload).unwrap(),
            vec![Frame::Ack {
                largest_acked: 10,
                ack_delay: 0,
                ranges: vec![(8, 10), (5, 6)],
                ecn: None
            }]
        );
    }

    #[test]
    fn ack_underflow_is_rejected() {
        let payload = [0x02, 0x01, 0x00, 0x00, 0x05];
        assert_eq!(
            parse_frames(&payload).unwrap_err(),
            Error::Invalid("first ack range (underflows)")
        );
    }

    #[test]
    fn unknown_frame_stops_iteration() {
        // 0x1f is not a frame type in RFC 9000 (0x1e is HANDSHAKE_DONE and
        // 0x30 starts the DATAGRAM range), so the decoder stops here rather
        // than guessing a length.
        let payload = [0x01, 0x1f, 0x01];
        let mut it = frames(&payload);
        assert_eq!(it.next().unwrap().unwrap(), Frame::Ping);
        assert_eq!(it.next().unwrap().unwrap_err(), Error::UnknownFrame(0x1f));
        assert!(it.next().is_none());
    }

    #[test]
    fn assembler_handles_gaps_overlap_and_duplicates() {
        let mut a = CryptoAssembler::new();
        assert!(a.is_empty());

        a.push(4, b"efgh");
        assert_eq!(a.contiguous(), b"");

        a.push(0, b"abcd");
        assert_eq!(a.contiguous(), b"abcdefgh");

        // Overlapping retransmission, and a duplicate.
        a.push(6, b"ghijkl");
        a.push(0, b"abcd");
        assert_eq!(a.contiguous(), b"abcdefghijkl");
    }
}
