//! Group `dirparams`: network parameters and the provider that hands them out
//! (`tor_netdir::params::NetParameters`, `tor_netdir::NetDirProvider`).
//!
//! The consensus carries integer parameters voted on by the directory
//! authorities, and a client must not trust them blindly: every value is
//! clamped into a range the client considers safe, unrecognized names are
//! dropped, and a missing name falls back to a compiled-in default. That
//! clamping is the whole safety story for the new channel-padding timers --
//! a hostile or buggy consensus that set `nf_ito_high` to two billion
//! milliseconds must not be able to park a client's padding timer past the
//! heat death of the universe. The provider half matters because consumers
//! need parameters even before any directory has arrived.
#![allow(non_snake_case)]

use std::sync::Arc;

use futures::stream::{BoxStream, StreamExt};
use tor_netdir::{
    params::{NetParameters, CHANNEL_PADDING_TIMEOUT_UPPER_BOUND},
    testnet, DirEvent, NetDir, NetDirProvider, Timeliness, UpcastArcNetDirProvider,
};

/// Build the standard test network with an explicit set of consensus params.
fn netdir_with_params(params: &[(&str, i32)]) -> NetDir {
    testnet::construct_custom_netdir_with_params(|_idx, _nb| {}, params.iter().copied())
        .expect("custom test network builds")
        .unwrap_if_sufficient()
        .expect("the test network has enough paths")
}

/// A minimal directory provider, as a consumer of the trait would see one.
struct Provider {
    dir: Arc<NetDir>,
}
impl NetDirProvider for Provider {
    fn netdir(&self, timeliness: Timeliness) -> tor_netdir::Result<Arc<NetDir>> {
        match timeliness {
            Timeliness::Strict => Err(tor_netdir::Error::DirExpired),
            _ => Ok(Arc::clone(&self.dir)),
        }
    }
    fn events(&self) -> BoxStream<'static, DirEvent> {
        Box::pin(futures::stream::iter(vec![
            DirEvent::NewConsensus,
            DirEvent::NewDescriptors,
        ]))
    }
    fn params(&self) -> Arc<dyn AsRef<NetParameters>> {
        Arc::clone(&self.dir) as Arc<dyn AsRef<NetParameters>>
    }
}

/// With no consensus at all, the defaults must still be usable values -- in
/// particular the four channel-padding timers this milestone introduces.
#[test]
fn dirparams__defaults_are_the_documented_values() {
    let p = NetParameters::default();

    assert_eq!(p.nf_ito_low.as_millis().get(), 1500);
    assert_eq!(p.nf_ito_high.as_millis().get(), 9500);
    assert_eq!(p.nf_ito_low_reduced.as_millis().get(), 9000);
    assert_eq!(p.nf_ito_high_reduced.as_millis().get(), 14000);

    // The safety ceiling that bounds all four.
    assert_eq!(CHANNEL_PADDING_TIMEOUT_UPPER_BOUND, 60_000);

    // A couple of unrelated defaults, so this is not just a padding check.
    assert_eq!(p.circuit_window.get(), 1000);
    assert_eq!(p.extend_by_ed25519_id.get(), 0);
    assert_eq!(p.cbt_timeout_quantile.as_percent().get(), 80);
}

/// A value present in the consensus replaces the default, and shows up through
/// `NetDir::params()`.
#[test]
fn dirparams__consensus_values_override_defaults() {
    let dir = netdir_with_params(&[
        ("nf_ito_low", 100),
        ("nf_ito_high", 200),
        ("circwindow", 500),
    ]);
    let p = dir.params();

    assert_eq!(p.nf_ito_low.as_millis().get(), 100);
    assert_eq!(p.nf_ito_high.as_millis().get(), 200);
    assert_eq!(p.circuit_window.get(), 500);
    // Parameters not mentioned keep their defaults.
    assert_eq!(
        p.nf_ito_high_reduced.as_millis().get(),
        NetParameters::default().nf_ito_high_reduced.as_millis().get()
    );
}

/// An out-of-range value is clamped to the nearest end of the permitted range
/// rather than rejected or wrapped. This is what keeps a bad consensus from
/// producing a nonsensical timer.
#[test]
fn dirparams__out_of_range_values_saturate() {
    let dir = netdir_with_params(&[
        ("nf_ito_high", 1_000_000),
        ("nf_ito_low", -5),
        ("circwindow", 1),
        ("cbtquantile", 1000),
    ]);
    let p = dir.params();

    assert_eq!(
        p.nf_ito_high.as_millis().get(),
        CHANNEL_PADDING_TIMEOUT_UPPER_BOUND,
        "a huge padding timeout is clamped to the safety ceiling"
    );
    assert_eq!(p.nf_ito_low.as_millis().get(), 0, "negative clamps to zero");
    assert_eq!(p.circuit_window.get(), 100, "below the floor clamps up");
    assert_eq!(p.cbt_timeout_quantile.as_percent().get(), 99);

    // i32 extremes do not overflow.
    let dir = netdir_with_params(&[("nf_ito_low", i32::MAX), ("nf_ito_high", i32::MIN)]);
    let p = dir.params();
    assert_eq!(p.nf_ito_low.as_millis().get(), CHANNEL_PADDING_TIMEOUT_UPPER_BOUND);
    assert_eq!(p.nf_ito_high.as_millis().get(), 0);
}

/// Parameters the client does not recognize are ignored. The consensus is a
/// shared document that evolves faster than any one client, so an unknown name
/// must not be an error, and must not disturb the names that are known.
#[test]
fn dirparams__unrecognized_parameters_are_ignored() {
    let dir = netdir_with_params(&[
        ("nf_ito_low", 1234),
        ("a-parameter-from-the-future", 7),
        ("NF_ITO_LOW", 9999),
    ]);
    let p = dir.params();

    assert_eq!(p.nf_ito_low.as_millis().get(), 1234);
    // Names are matched exactly; a differently-cased spelling is a different
    // (and here, unrecognized) parameter.
    assert_eq!(p.nf_ito_high.as_millis().get(), 9500);
}

/// A `NetDir` is itself a source of parameters via `AsRef`, which is what lets
/// a provider hand out `Arc<dyn AsRef<NetParameters>>` without copying the
/// parameter block or leaking the whole directory type into every consumer.
#[test]
fn dirparams__netdir_is_a_parameter_source() {
    let dir = Arc::new(netdir_with_params(&[("nf_ito_low", 42)]));
    let provider = Provider {
        dir: Arc::clone(&dir),
    };

    let direct: &NetParameters = dir.as_ref().as_ref();
    assert_eq!(direct.nf_ito_low.as_millis().get(), 42);

    let via_provider = provider.params();
    assert_eq!(
        via_provider.as_ref().as_ref().nf_ito_low.as_millis().get(),
        42
    );
    // The handle stays valid independently of the provider.
    drop(provider);
    assert_eq!(via_provider.as_ref().as_ref().circuit_window.get(), 1000);
}

/// The provider interface: `timely_netdir` is defined in terms of `netdir`, an
/// `Arc<T>` forwards the whole interface, the event stream reports what
/// changed, and the timeliness argument is honoured rather than ignored.
#[test]
fn dirparams__provider_interface_forwards_and_upcasts() {
    let dir = Arc::new(netdir_with_params(&[]));
    let provider = Arc::new(Provider {
        dir: Arc::clone(&dir),
    });

    // `Strict` is refused by this provider; `timely_netdir` asks for `Timely`.
    assert!(provider.netdir(Timeliness::Strict).is_err());
    assert!(provider.netdir(Timeliness::Unchecked).is_ok());
    assert!(
        provider.timely_netdir().is_ok(),
        "timely_netdir must ask for Timeliness::Timely"
    );

    // `Arc<T>` is itself a provider, forwarding every method.
    let as_provider: &dyn NetDirProvider = &provider;
    assert!(as_provider.timely_netdir().is_ok());

    // ...and can be upcast to a trait object without naming the concrete type.
    let upcast: Arc<dyn NetDirProvider> = Arc::clone(&provider).upcast_arc();
    assert!(upcast.timely_netdir().is_ok());
    assert_eq!(
        upcast.params().as_ref().as_ref().nf_ito_high.as_millis().get(),
        9500
    );

    let events: Vec<DirEvent> = futures::executor::block_on(upcast.events().collect());
    assert_eq!(
        events,
        vec![DirEvent::NewConsensus, DirEvent::NewDescriptors]
    );
}
