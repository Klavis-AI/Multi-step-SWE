//! Group `rtsend`: `Send` on a runtime's incoming-connection stream.
//!
//! At this milestone `TcpListener::Incoming` gains a `Send` bound:
//!
//! ```text
//! type Incoming: Stream<Item = IoResult<(Self::TcpStream, SocketAddr)>> + Send + Unpin;
//! ```
//!
//! Without it, a listener's incoming stream cannot be moved into a spawned
//! task or boxed as `dyn Stream + Send`, which is what a service accepting
//! connections needs to do. The bound is a property of the *trait*, so the
//! cases below assert it generically -- through a type parameter, not through
//! the concrete listener -- since a concrete stream may happen to be `Send`
//! whether or not the trait requires it.
//!
//! No network is involved: the listener below is a local fake that satisfies
//! the trait's obligations.
#![allow(non_snake_case)]

use std::io::Result as IoResult;
use std::net::SocketAddr;

use async_trait::async_trait;
use futures::stream::{self, Stream, StreamExt};
use tor_rtcompat::TcpListener;

/// A listener that hands out in-memory connections, so the trait's contract
/// can be exercised without a socket.
struct FakeListener {
    /// The address this listener claims to be bound to.
    addr: SocketAddr,
}

/// The in-memory stand-in for a TCP connection.
type FakeStream = futures::io::Cursor<Vec<u8>>;

impl FakeListener {
    /// A listener bound to a fixed, never-used address.
    fn new() -> Self {
        FakeListener {
            addr: "127.0.0.1:9150".parse().expect("a literal socket address"),
        }
    }

    /// One pending connection, as `accept` and `incoming` both report it.
    fn connection(&self) -> (FakeStream, SocketAddr) {
        (
            futures::io::Cursor::new(b"hello".to_vec()),
            "127.0.0.1:1234".parse().expect("a literal socket address"),
        )
    }
}

#[async_trait]
impl TcpListener for FakeListener {
    type TcpStream = FakeStream;
    type Incoming = stream::Iter<std::vec::IntoIter<IoResult<(FakeStream, SocketAddr)>>>;

    async fn accept(&self) -> IoResult<(Self::TcpStream, SocketAddr)> {
        Ok(self.connection())
    }

    fn incoming(self) -> Self::Incoming {
        let conn = self.connection();
        stream::iter(vec![Ok(conn)])
    }

    fn local_addr(&self) -> IoResult<SocketAddr> {
        Ok(self.addr)
    }
}

/// Assert, at compile time, that `T` is `Send`.
fn require_send<T: Send + ?Sized>() {}

/// Assert, at compile time, that `T` is `Unpin`.
fn require_unpin<T: Unpin + ?Sized>() {}

/// Every `TcpListener`'s incoming stream is `Send` and `Unpin`, whatever the
/// concrete listener is: these calls resolve against the trait's declared
/// bounds, not against `FakeListener`'s associated types.
#[test]
fn rtsend__incoming_stream_is_send_for_every_listener() {
    fn check<L: TcpListener>() {
        require_send::<L::Incoming>();
        require_unpin::<L::Incoming>();
        // The connection type carries its own bounds, which predate this
        // milestone; checking them here proves the new bound was added rather
        // than moved from somewhere else.
        require_send::<L::TcpStream>();
        require_unpin::<L::TcpStream>();
    }
    check::<FakeListener>();
}

/// Because the stream is `Send`, it can be boxed as `dyn Stream + Send + Unpin`
/// -- the form an accept loop hands to a spawned task.
#[test]
fn rtsend__incoming_stream_boxes_as_dyn_send() {
    // Note which bounds are spelled out here and which are not: `'static`
    // must be requested, but `Send` and `Unpin` are not, because the trait
    // already promises them.
    fn erase<L: TcpListener>(
        listener: L,
    ) -> Box<dyn Stream<Item = IoResult<(L::TcpStream, SocketAddr)>> + Send + Unpin>
    where
        L::TcpStream: 'static,
        L::Incoming: 'static,
    {
        Box::new(listener.incoming())
    }

    let mut boxed = erase(FakeListener::new());
    let (stream, peer) = futures::executor::block_on(boxed.next())
        .expect("one connection is queued")
        .expect("and it is not an error");
    assert_eq!(peer.port(), 1234, "the peer address travels with the stream");
    assert_eq!(stream.into_inner(), b"hello".to_vec());
    assert!(
        futures::executor::block_on(boxed.next()).is_none(),
        "the queue is drained"
    );
}

/// The rest of the listener contract is unchanged: `accept` yields a
/// connection and its peer address, and `local_addr` reports the bound
/// address.
#[test]
fn rtsend__listener_reports_addresses() {
    let listener = FakeListener::new();
    assert_eq!(
        listener.local_addr().expect("a bound address"),
        "127.0.0.1:9150".parse::<SocketAddr>().unwrap()
    );

    let (stream, peer) = futures::executor::block_on(listener.accept()).expect("a connection");
    assert_eq!(peer, "127.0.0.1:1234".parse::<SocketAddr>().unwrap());
    assert_eq!(stream.into_inner(), b"hello".to_vec());

    // `accept` is itself an async method with a `Send` future, so an accept
    // loop can live in a spawned task.
    require_send::<futures::future::BoxFuture<'_, IoResult<(FakeStream, SocketAddr)>>>();
}
