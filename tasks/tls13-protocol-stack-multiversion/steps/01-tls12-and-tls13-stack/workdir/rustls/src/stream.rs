use crate::session::Session;
use std::io::{IoSlice, Read, Result, Write};

/// This type implements `io::Read` and `io::Write`, encapsulating
/// a Session `S` and an underlying transport `T`, such as a socket.
///
/// This allows you to use a rustls Session like a normal stream.
pub struct Stream<'a, S: 'a + Session + ?Sized, T: 'a + Read + Write + ?Sized> {
    /// Our session
    pub sess: &'a mut S,

    /// The underlying transport, like a socket
    pub sock: &'a mut T,
}

impl<'a, S, T> Stream<'a, S, T>
where
    S: 'a + Session,
    T: 'a + Read + Write,
{
    /// Make a new Stream using the Session `sess` and socket-like object
    /// `sock`.  This does not fail and does no IO.
    pub fn new(sess: &'a mut S, sock: &'a mut T) -> Stream<'a, S, T> {
        unimplemented!()
    }

    /// If we're handshaking, complete all the IO for that.
    /// If we have data to write, write it all.
    fn complete_prior_io(&mut self) -> Result<()> {
        unimplemented!()
    }
}

impl<'a, S, T> Read for Stream<'a, S, T>
where
    S: 'a + Session,
    T: 'a + Read + Write,
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        unimplemented!()
    }
}

impl<'a, S, T> Write for Stream<'a, S, T>
where
    S: 'a + Session,
    T: 'a + Read + Write,
{
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        unimplemented!()
    }

    fn write_vectored(&mut self, bufs: &[IoSlice<'_>]) -> Result<usize> {
        unimplemented!()
    }

    fn flush(&mut self) -> Result<()> {
        unimplemented!()
    }
}

/// This type implements `io::Read` and `io::Write`, encapsulating
/// and owning a Session `S` and an underlying blocking transport
/// `T`, such as a socket.
///
/// This allows you to use a rustls Session like a normal stream.
pub struct StreamOwned<S: Session + Sized, T: Read + Write + Sized> {
    /// Our session
    pub sess: S,

    /// The underlying transport, like a socket
    pub sock: T,
}

impl<S, T> StreamOwned<S, T>
where
    S: Session,
    T: Read + Write,
{
    /// Make a new StreamOwned taking the Session `sess` and socket-like
    /// object `sock`.  This does not fail and does no IO.
    ///
    /// This is the same as `Stream::new` except `sess` and `sock` are
    /// moved into the StreamOwned.
    pub fn new(sess: S, sock: T) -> StreamOwned<S, T> {
        unimplemented!()
    }

    /// Get a reference to the underlying socket
    pub fn get_ref(&self) -> &T {
        unimplemented!()
    }

    /// Get a mutable reference to the underlying socket
    pub fn get_mut(&mut self) -> &mut T {
        unimplemented!()
    }
}

impl<'a, S, T> StreamOwned<S, T>
where
    S: Session,
    T: Read + Write,
{
    fn as_stream(&'a mut self) -> Stream<'a, S, T> {
        unimplemented!()
    }
}

impl<S, T> Read for StreamOwned<S, T>
where
    S: Session,
    T: Read + Write,
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        unimplemented!()
    }
}

impl<S, T> Write for StreamOwned<S, T>
where
    S: Session,
    T: Read + Write,
{
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        unimplemented!()
    }

    fn flush(&mut self) -> Result<()> {
        unimplemented!()
    }
}

#[cfg(test)]
mod tests {
    use super::{Stream, StreamOwned};
    use crate::client::ClientSession;
    use crate::server::ServerSession;
    use crate::session::Session;
    use std::net::TcpStream;

    #[test]
    fn stream_can_be_created_for_session_and_tcpstream() {
        unimplemented!()
    }

    #[test]
    fn streamowned_can_be_created_for_client_and_tcpstream() {
        unimplemented!()
    }

    #[test]
    fn streamowned_can_be_created_for_server_and_tcpstream() {
        unimplemented!()
    }
}
