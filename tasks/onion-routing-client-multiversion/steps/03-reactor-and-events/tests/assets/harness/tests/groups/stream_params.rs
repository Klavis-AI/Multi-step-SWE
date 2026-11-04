//! Group `stream_params` - the new per-stream preference object.
//!
//! instruction.md section 1: "A per-stream optimistic-data preference".
//! Whether a stream is optimistic became a per-stream preference, and
//! `ClientCirc::begin_stream` now takes that preference object rather than a
//! bare IP-version flag. The directory entry point keeps its own, fixed
//! answer and takes no preference object at all.
//!
//! Graded items: `tor_proto::stream::StreamParameters::{new, ip_version,
//! optimistic}`, its `Default`/`Clone`/`Debug` implementations, and the fourth
//! parameter of `tor_proto::circuit::ClientCirc::begin_stream`.
//!
//! The fields are private and have no accessors, so the configured state is
//! read back through the derived `Debug` representation, which names each
//! field and its value.

use tor_proto::stream::{IpVersionPreference, StreamParameters};

#[test]
fn stream_params__new_matches_default() {
    assert_eq!(
        format!("{:?}", StreamParameters::new()),
        format!("{:?}", StreamParameters::default())
    );
    let p = StreamParameters::new();
    assert_eq!(format!("{:?}", p.clone()), format!("{:?}", p));
    assert!(format!("{:?}", p).starts_with("StreamParameters"));
}

#[test]
fn stream_params__default_is_non_optimistic_ipv4_preferred() {
    let repr = format!("{:?}", StreamParameters::default());
    assert!(
        repr.contains("Ipv4Preferred"),
        "default IP preference should be Ipv4Preferred, got {:?}",
        repr
    );
    assert!(
        repr.contains("optimistic: false"),
        "streams should not be optimistic by default, got {:?}",
        repr
    );
}

#[test]
fn stream_params__optimistic_is_settable() {
    let mut p = StreamParameters::new();
    p.optimistic(true);
    let repr = format!("{:?}", p);
    assert!(
        repr.contains("optimistic: true"),
        "optimistic(true) was not recorded, got {:?}",
        repr
    );
    assert_ne!(repr, format!("{:?}", StreamParameters::default()));

    // ... and settable back again.
    p.optimistic(false);
    assert_eq!(
        format!("{:?}", p),
        format!("{:?}", StreamParameters::default())
    );
}

#[test]
fn stream_params__setters_chain_and_record_ip_version() {
    let mut p = StreamParameters::new();
    // Each setter returns `&mut Self`, so they chain.
    let r: &mut StreamParameters = p.ip_version(IpVersionPreference::Ipv6Only).optimistic(true);
    let repr = format!("{:?}", r);
    assert!(repr.contains("Ipv6Only"), "ip_version not recorded: {:?}", repr);
    assert!(repr.contains("optimistic: true"), "optimistic not recorded: {:?}", repr);

    for pref in [
        IpVersionPreference::Ipv4Only,
        IpVersionPreference::Ipv4Preferred,
        IpVersionPreference::Ipv6Preferred,
        IpVersionPreference::Ipv6Only,
    ] {
        let mut q = StreamParameters::new();
        q.ip_version(pref);
        assert!(
            format!("{:?}", q).contains(&format!("{:?}", pref)),
            "ip_version({:?}) was not recorded",
            pref
        );
    }
}

#[test]
fn stream_params__begin_stream_takes_the_parameter_object() {
    use std::sync::Arc;
    use tor_proto::circuit::ClientCirc;

    // `begin_stream` accepts stream preferences, not a bare IP-version flag.
    // Checked at the type level: no circuit can be constructed without a live
    // relay connection.
    #[allow(dead_code)]
    async fn call_shape(
        circ: Arc<ClientCirc>,
        target: &str,
        port: u16,
        params: Option<StreamParameters>,
    ) -> tor_proto::Result<tor_proto::stream::DataStream> {
        circ.begin_stream(target, port, params).await
    }

    let mut params = StreamParameters::new();
    params.optimistic(true);
    let opt: Option<StreamParameters> = Some(params);
    assert!(format!("{:?}", opt).contains("optimistic: true"));
}

#[test]
fn stream_params__directory_streams_take_no_parameter_object() {
    use std::sync::Arc;
    use tor_proto::circuit::ClientCirc;

    // "Optimistic data, on by default for directory requests" is not expressed
    // through `StreamParameters` at all. The directory path is its own entry
    // point, and it takes no preference object: a caller cannot ask for a
    // non-optimistic directory stream, and cannot ask for an optimistic one
    // either, because the decision is not the caller's to make.
    //
    // The signature is the graded observable, and it is graded by compiling
    // this call: the name, the `Arc<Self>` receiver, the empty parameter list
    // and the return type all have to match. Whether the stream is handed
    // back before the far end confirms is *not* graded - that needs a built
    // circuit, and the accessor it consults is `pub(crate)`. See
    // TEST_INTERFACE section 5.2.
    #[allow(dead_code)]
    async fn dir_call_shape(
        circ: Arc<ClientCirc>,
    ) -> tor_proto::Result<tor_proto::stream::DataStream> {
        circ.begin_dir_stream().await
    }

    // The two entry points agree on the stream type they produce, and only the
    // general one is configurable.
    #[allow(dead_code)]
    async fn general_call_shape(
        circ: Arc<ClientCirc>,
    ) -> tor_proto::Result<tor_proto::stream::DataStream> {
        circ.begin_stream("example.com", 80, Some(StreamParameters::new()))
            .await
    }

    // Nothing is asserted at run time here on purpose: the only runtime fact
    // available - that `StreamParameters::default()` is non-optimistic - is
    // already asserted by
    // `stream_params__default_is_non_optimistic_ipv4_preferred` in this same
    // group, and groups are all-or-nothing, so restating it adds no
    // discrimination. The two shapes above are the case.
}
