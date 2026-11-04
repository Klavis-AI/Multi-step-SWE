//! Group `udpruntime`: UDP support in the runtime abstraction.  A DNS port
//! cannot be served over TCP alone, so `Runtime` now also requires a UDP
//! provider, and the sockets it hands out report the peer address of each
//! datagram (the resolver keys its isolation on that address).
//!
//! Every socket in this group is bound to `127.0.0.1` with a kernel-chosen
//! port; nothing here touches the network.
//!
//! Graded items: `tor_rtcompat::{UdpProvider, UdpSocket}` and the
//! `Runtime: UdpProvider` supertrait bound.

use std::net::SocketAddr;

use tor_rtcompat::tokio::TokioNativeTlsRuntime;
use tor_rtcompat::{BlockOn, Runtime, UdpProvider, UdpSocket};

/// An unused loopback address with a kernel-chosen port.
fn loopback() -> SocketAddr {
    "127.0.0.1:0".parse().expect("a valid socket address")
}

/// Create a runtime for one case.
fn runtime() -> TokioNativeTlsRuntime {
    TokioNativeTlsRuntime::create().expect("a runtime can be created")
}

#[test]
fn udpruntime__a_runtime_can_bind_a_local_udp_socket() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let sock = rt.bind(&loopback()).await.expect("binding loopback works");
        let local = sock.local_addr().expect("a bound socket has a local address");
        assert!(local.ip().is_loopback(), "bound to {}", local);
        assert_ne!(local.port(), 0, "the kernel must assign a real port");

        // A second socket gets a different port.
        let other = rt.bind(&loopback()).await.expect("binding loopback works");
        let other_local = other.local_addr().expect("a bound socket has an address");
        assert_ne!(local.port(), other_local.port());
    });
}

#[test]
fn udpruntime__a_datagram_is_delivered_verbatim() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let server = rt.bind(&loopback()).await.expect("bind server");
        let client = rt.bind(&loopback()).await.expect("bind client");
        let server_addr = server.local_addr().expect("server address");

        let payload: Vec<u8> = (0u8..=255).collect();
        let sent = client
            .send(&payload, &server_addr)
            .await
            .expect("sending a datagram works");
        assert_eq!(sent, payload.len(), "send reports the byte count");

        let mut buf = [0u8; 1536];
        let (len, _from) = server.recv(&mut buf).await.expect("receiving works");
        assert_eq!(len, payload.len());
        assert_eq!(&buf[..len], &payload[..]);
    });
}

#[test]
fn udpruntime__the_peer_address_of_each_datagram_is_reported() {
    let rt = runtime();
    rt.clone().block_on(async move {
        let server = rt.bind(&loopback()).await.expect("bind server");
        let a = rt.bind(&loopback()).await.expect("bind peer a");
        let b = rt.bind(&loopback()).await.expect("bind peer b");
        let server_addr = server.local_addr().expect("server address");
        let a_addr = a.local_addr().expect("peer a address");
        let b_addr = b.local_addr().expect("peer b address");
        assert_ne!(a_addr, b_addr);

        a.send(b"from-a", &server_addr).await.expect("a sends");
        let mut buf = [0u8; 64];
        let (len, from) = server.recv(&mut buf).await.expect("server receives");
        assert_eq!(&buf[..len], b"from-a");
        assert_eq!(from, a_addr, "the reported peer must be the real sender");

        b.send(b"from-b", &server_addr).await.expect("b sends");
        let (len, from) = server.recv(&mut buf).await.expect("server receives");
        assert_eq!(&buf[..len], b"from-b");
        assert_eq!(from, b_addr);
    });
}

#[test]
fn udpruntime__a_socket_can_answer_the_peer_it_heard_from() {
    // This is the shape the DNS resolver uses: receive a query, then send the
    // answer back to the address the query came from.
    let rt = runtime();
    rt.clone().block_on(async move {
        let server = rt.bind(&loopback()).await.expect("bind server");
        let client = rt.bind(&loopback()).await.expect("bind client");
        let server_addr = server.local_addr().expect("server address");

        client.send(b"query", &server_addr).await.expect("query");
        let mut buf = [0u8; 64];
        let (len, from) = server.recv(&mut buf).await.expect("server receives");
        assert_eq!(&buf[..len], b"query");

        server.send(b"answer", &from).await.expect("answer");
        let (len, from) = client.recv(&mut buf).await.expect("client receives");
        assert_eq!(&buf[..len], b"answer");
        assert_eq!(from, server_addr);
    });
}

#[test]
fn udpruntime__every_runtime_provides_udp_sockets() {
    // `Runtime` requires `UdpProvider`, so a function generic over `Runtime`
    // alone can bind a UDP socket.  This would not compile if the bound were
    // missing.
    fn bind_with_any_runtime<R: Runtime>(rt: R) -> SocketAddr {
        rt.clone().block_on(async move {
            let sock = rt.bind(&loopback()).await.expect("a Runtime can bind UDP");
            sock.local_addr().expect("bound socket has an address")
        })
    }

    let addr = bind_with_any_runtime(runtime());
    assert!(addr.ip().is_loopback());
    assert_ne!(addr.port(), 0);
}
