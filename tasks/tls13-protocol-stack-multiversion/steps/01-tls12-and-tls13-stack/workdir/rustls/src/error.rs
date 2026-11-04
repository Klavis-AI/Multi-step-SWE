use crate::msgs::enums::{AlertDescription, ContentType, HandshakeType};
use sct;
use std::error::Error;
use std::fmt;
use webpki;

/// rustls reports protocol errors using this type.
#[derive(Debug, PartialEq, Clone)]
pub enum TLSError {
    /// We received a TLS message that isn't valid right now.
    /// `expect_types` lists the message types we can expect right now.
    /// `got_type` is the type we found.  This error is typically
    /// caused by a buggy TLS stack (the peer or this one), a broken
    /// network, or an attack.
    InappropriateMessage {
        /// Which types we expected
        expect_types: Vec<ContentType>,
        /// What type we received
        got_type: ContentType,
    },

    /// We received a TLS handshake message that isn't valid right now.
    /// `expect_types` lists the handshake message types we can expect
    /// right now.  `got_type` is the type we found.
    InappropriateHandshakeMessage {
        /// Which handshake type we expected
        expect_types: Vec<HandshakeType>,
        /// What handshake type we received
        got_type: HandshakeType,
    },

    /// The peer sent us a syntactically incorrect TLS message.
    CorruptMessage,

    /// The peer sent us a TLS message with invalid contents.
    CorruptMessagePayload(ContentType),

    /// The peer didn't give us any certificates.
    NoCertificatesPresented,

    /// We couldn't decrypt a message.  This is invariably fatal.
    DecryptError,

    /// The peer doesn't support a protocol version/feature we require.
    /// The parameter gives a hint as to what version/feature it is.
    PeerIncompatibleError(String),

    /// The peer deviated from the standard TLS protocol.
    /// The parameter gives a hint where.
    PeerMisbehavedError(String),

    /// We received a fatal alert.  This means the peer is unhappy.
    AlertReceived(AlertDescription),

    /// The presented certificate chain is invalid.
    WebPKIError(webpki::Error),

    /// The presented SCT(s) were invalid.
    InvalidSCT(sct::Error),

    /// A catch-all error for unlikely errors.
    General(String),

    /// We failed to figure out what time it currently is.
    FailedToGetCurrentTime,

    /// This function doesn't work until the TLS handshake
    /// is complete.
    HandshakeNotComplete,

    /// The peer sent an oversized record/fragment.
    PeerSentOversizedRecord,

    /// An incoming connection did not support any known application protocol.
    NoApplicationProtocol,
}

fn join<T: fmt::Debug>(items: &[T]) -> String {
    unimplemented!()
}

impl fmt::Display for TLSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        unimplemented!()
    }
}

impl Error for TLSError {}

#[cfg(test)]
mod tests {
    #[test]
    fn smoke() {
        unimplemented!()
    }
}
