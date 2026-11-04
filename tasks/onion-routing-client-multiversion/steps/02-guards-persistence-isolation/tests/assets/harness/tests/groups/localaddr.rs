//! Group `localaddr` - `arti_client::TorAddr` and the client's address policy.
//!
//! Graded through `arti_client::{TorAddr, TorAddrError, IntoTorAddr,
//! DangerouslyIntoTorAddr, Error}`, `arti_client::config::{ClientAddrConfig,
//! ClientAddrConfigBuilder}` and `arti_client::TorClientConfig`. A client run
//! as a SOCKS proxy is handed target addresses by untrusted local programs, so
//! it has to distinguish a name from a literal address, never resolve the name
//! itself, and refuse to reach back into the local network unless it was
//! explicitly configured to.
//!
//! The predicate that applies the policy to a parsed address
//! (`TorAddr::enforce_config`) is crate-private at this checkpoint, so what is
//! graded here is the public half: the parse, the policy object and the
//! refusals the policy produces.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6};
use std::str::FromStr;
use arti_client::config::{ClientAddrConfig, ClientAddrConfigBuilder};
use arti_client::{
    DangerouslyIntoTorAddr, Error, IntoTorAddr, TorAddr, TorAddrError, TorClientConfig,
};

/// Build a `TorAddr` from a string, panicking on failure.
fn tor(host: &str) -> TorAddr {
    host.into_tor_addr()
        .unwrap_or_else(|e| panic!("{:?} was not accepted as an address: {}", host, e))
}

#[test]
fn localaddr__a_name_and_a_literal_address_are_told_apart() {
    let by_name = tor("example.com:443");
    assert!(!by_name.is_ip_address(), "a hostname is not an IP address");
    assert_eq!(by_name.to_string(), "example.com:443");

    let by_ip = tor("198.51.100.4:443");
    assert!(by_ip.is_ip_address());
    assert_eq!(by_ip.to_string(), "198.51.100.4:443");

    // A literal IPv6 address has to keep its brackets when rendered, since
    // its own colons are otherwise indistinguishable from the port separator.
    let v6 = tor("[2001:db8::42]:80");
    assert!(v6.is_ip_address());
    assert_eq!(v6.to_string(), "[2001:db8::42]:80");
    assert_eq!(
        v6,
        (IpAddr::V6("2001:db8::42".parse::<Ipv6Addr>().expect("bad fixture")), 80)
            .into_tor_addr_dangerously()
            .expect("a literal IPv6 address was refused")
    );
}

#[test]
fn localaddr__an_address_without_a_port_is_not_an_address() {
    match "example.com".into_tor_addr() {
        Ok(a) => panic!("a bare hostname was accepted: {}", a),
        Err(e) => {
            assert!(matches!(e, TorAddrError::NoPort), "got {:?}", e);
        }
    }
    // The same failure reaches the `FromStr` and `TorAddr::from` fronts.
    assert!(matches!(
        TorAddr::from_str("example.com"),
        Err(TorAddrError::NoPort)
    ));
    assert!(matches!(
        TorAddr::from("example.com"),
        Err(TorAddrError::NoPort)
    ));
}

#[test]
fn localaddr__a_port_that_is_not_a_number_is_reported_separately() {
    match "example.com:http".into_tor_addr() {
        Ok(a) => panic!("a non-numeric port was accepted: {}", a),
        Err(e) => {
            assert!(matches!(e, TorAddrError::BadPort), "got {:?}", e);
        }
    }
    // The split is on the last colon, so a bracketed literal with an
    // unparseable port is a port failure and not a host failure.
    assert!(matches!(
        "[2001:db8::1]:http".into_tor_addr(),
        Err(TorAddrError::BadPort)
    ));

    // An empty port is one that cannot be parsed, not a missing one.
    assert!(matches!("example.com:".into_tor_addr(), Err(TorAddrError::BadPort)));

    // A port that does not fit in sixteen bits is out of range.
    assert!(matches!(
        "example.com:65536".into_tor_addr(),
        Err(TorAddrError::BadPort)
    ));

    // An empty host is still a host: this layer parses, it does not judge,
    // so only a port can make the parse itself fail.
    let empty_host = ":443".into_tor_addr().expect("an empty host failed to parse");
    assert!(!empty_host.is_ip_address(), "an empty host became an address");
    assert_eq!(empty_host.to_string(), ":443");
}

#[test]
fn localaddr__the_dangerous_constructors_skip_the_name_machinery() {
    // An address that already came from a literal has no name to parse, so
    // the unsafe-by-contract constructors accept it whatever it is.
    let quad: SocketAddr = "1.1.1.1:53".parse().expect("bad fixture");
    let from_socket = quad
        .into_tor_addr_dangerously()
        .expect("a SocketAddr was refused");
    assert!(from_socket.is_ip_address());
    assert_eq!(from_socket.to_string(), "1.1.1.1:53");

    let from_ip = (IpAddr::V4(Ipv4Addr::new(1, 1, 1, 1)), 53)
        .into_tor_addr_dangerously()
        .expect("an IpAddr was refused");
    assert_eq!(from_ip, from_socket);

    let from_v4 = (Ipv4Addr::new(1, 1, 1, 1), 53).into_tor_addr_dangerously();
    let from_v6 = (Ipv6Addr::LOCALHOST, 53).into_tor_addr_dangerously();
    assert_eq!(from_v4.expect("an Ipv4Addr was refused"), from_ip);
    assert!(from_v6.is_ok());

    // A `TorAddr` is already a `TorAddr`.
    assert_eq!(from_ip.clone().into_tor_addr().expect("identity failed"), from_ip);
}

#[test]
fn localaddr__the_local_address_policy_is_off_unless_it_is_named() {
    // The option that admits local targets is off in the safe default, and
    // the field that carries it is named `allow_local_addrs`. The field is
    // `pub(crate)` with no accessor, so the derived `Debug` is the only
    // channel there is -- see TEST_INTERFACE.md section 7.1, which states that
    // only the field name and its value are pinned, never the struct name or
    // the exact spacing of the derived rendering.
    let cfg = ClientAddrConfig::default();
    assert!(
        format!("{:?}", cfg).contains("allow_local_addrs: false"),
        "the default address policy is not the safe one: {:?}",
        cfg
    );

    // Building one without naming the option gives the same safe value ...
    let unset = ClientAddrConfigBuilder::default()
        .build()
        .expect("an address config with nothing set failed to build");
    assert_eq!(format!("{:?}", unset), format!("{:?}", cfg));

    // ... and naming it is the only way to turn it on.
    let permissive = ClientAddrConfigBuilder::default()
        .allow_local_addrs(true)
        .build()
        .expect("building a permissive address config failed");
    assert!(
        format!("{:?}", permissive).contains("allow_local_addrs: true"),
        "naming the option did not turn it on: {:?}",
        permissive
    );

    // A whole client configuration inherits the safe policy, so a client
    // that was never configured refuses local targets.
    let dir = crate::common::tempdir();
    let whole = TorClientConfig::with_directories(dir.path().join("state"), dir.path().join("cache"))
        .expect("a client configuration could not be built");
    assert!(
        format!("{:?}", whole).contains("allow_local_addrs: false"),
        "the client-wide default is not the safe one: {:?}",
        whole
    );
}

#[test]
fn localaddr__the_refusals_the_address_rules_produce_are_told_apart() {
    // Reaching back into the local network, a name that can never be a
    // hostname, and an onion address are three DISTINCT refusals, not one
    // catch-all. Their wording is a diagnostic and is not graded; their
    // identity is. `arti_client::Error` is not `PartialEq` at this release,
    // so `Debug` is the only thing that tells two unit variants apart.
    let refusals = [
        Error::LocalAddress,
        Error::InvalidHostname,
        Error::OnionAddressNotSupported,
    ];
    for (i, a) in refusals.iter().enumerate() {
        assert!(
            !a.to_string().is_empty(),
            "an address refusal rendered as nothing: {:?}",
            a
        );
        for b in refusals.iter().skip(i + 1) {
            assert_ne!(
                format!("{:?}", a),
                format!("{:?}", b),
                "two address refusals are the same variant"
            );
        }
    }

    // A failure to parse the address at all is carried through in its own
    // variant, which WRAPS the `TorAddrError` rather than flattening it, so a
    // caller can still see which parse rule was broken.
    let e: Error = TorAddrError::NoPort.into();
    assert!(matches!(e, Error::Address(TorAddrError::NoPort)), "got {:?}", e);
    let e: Error = TorAddrError::BadPort.into();
    assert!(matches!(e, Error::Address(TorAddrError::BadPort)), "got {:?}", e);
    let e: Error = TorAddrError::InvalidHostname.into();
    assert!(
        matches!(e, Error::Address(TorAddrError::InvalidHostname)),
        "got {:?}",
        e
    );

    // And a parse failure is not the same thing as the client's own
    // `InvalidHostname` policy refusal, even though both mention a hostname.
    assert_ne!(
        format!("{:?}", Error::from(TorAddrError::InvalidHostname)),
        format!("{:?}", Error::InvalidHostname),
        "a parse failure was flattened into the policy refusal"
    );
}

#[test]
fn localaddr__the_host_and_the_port_may_be_handed_over_apart() {
    // A host and a port given separately name the same target as the
    // colon-joined string: the port is not part of the name.
    assert_eq!(("example.com", 443).into_tor_addr().expect("tuple refused"), tor("example.com:443"));
    assert_eq!(
        ("example.com".to_string(), 443)
            .into_tor_addr()
            .expect("owned tuple refused"),
        tor("example.com:443")
    );

    // An address literal handed over as a bare string is still recognised as
    // a literal, even though it arrived without brackets, and it is rendered
    // back with them.
    let v6 = ("2001:db8::42", 20).into_tor_addr().expect("a literal tuple was refused");
    assert!(v6.is_ip_address(), "an unbracketed IPv6 literal became a name");
    assert_eq!(v6.to_string(), "[2001:db8::42]:20");
    assert_eq!(v6, tor("[2001:db8::42]:20"));

    let v4 = ("198.51.100.4", 443).into_tor_addr().expect("a literal tuple was refused");
    assert!(v4.is_ip_address());
    assert_eq!(v4.to_string(), "198.51.100.4:443");

    // `TorAddr::from` and `TorAddr::dangerously_from` are the same two
    // doors, named as constructors.
    assert_eq!(
        TorAddr::from(("example.com", 443)).expect("TorAddr::from refused a tuple"),
        tor("example.com:443")
    );
    assert_eq!(
        TorAddr::dangerously_from(SocketAddrV4::new(Ipv4Addr::new(198, 51, 100, 4), 443))
            .expect("TorAddr::dangerously_from refused a SocketAddrV4"),
        v4
    );
    assert_eq!(
        TorAddr::dangerously_from(SocketAddrV6::new(
            "2001:db8::42".parse().expect("bad fixture"),
            20,
            0,
            0
        ))
        .expect("TorAddr::dangerously_from refused a SocketAddrV6"),
        v6
    );

    // A borrowed value goes through the same door as an owned one.
    let owned = tor("example.com:443");
    assert_eq!((&owned).into_tor_addr().expect("a borrow was refused"), owned);
    let sock: SocketAddr = "198.51.100.4:443".parse().expect("bad fixture");
    assert_eq!(
        (&sock).into_tor_addr_dangerously().expect("a borrowed SocketAddr was refused"),
        v4
    );

    // The largest port a request can name survives the tuple form.
    assert_eq!(
        ("example.com", 65535).into_tor_addr().expect("port 65535 refused").to_string(),
        "example.com:65535"
    );
}
