//! Directory-provider timeliness levels: `Timeliness`, the `NetDirProvider`
//! methods that take one, and the netdir error kinds that describe untimeliness.

#![allow(non_snake_case)]

use std::sync::{Arc, Mutex};

use futures::stream::{self, BoxStream, StreamExt};
use tor_error::{ErrorKind, HasKind};
use tor_netdir::{DirEvent, Error, NetDir, NetDirProvider, Timeliness, UpcastArcNetDirProvider};

/// A provider that records the timeliness level it was asked for.
///
/// It never has a directory, which is exactly the state this group is about:
/// the caller's requested tolerance must reach the provider unchanged.
#[derive(Default)]
struct Recorder {
    seen: Mutex<Vec<Timeliness>>,
}

impl Recorder {
    fn seen(&self) -> Vec<Timeliness> {
        self.seen.lock().expect("lock").clone()
    }
}

impl NetDirProvider for Recorder {
    fn netdir(&self, timeliness: Timeliness) -> tor_netdir::Result<Arc<NetDir>> {
        self.seen.lock().expect("lock").push(timeliness);
        Err(Error::NoInfo)
    }

    fn events(&self) -> BoxStream<'static, DirEvent> {
        stream::empty().boxed()
    }
}

#[test]
fn timeliness__has_three_distinct_levels() {
    let levels = [Timeliness::Strict, Timeliness::Timely, Timeliness::Unchecked];
    assert_ne!(levels[0], levels[1]);
    assert_ne!(levels[1], levels[2]);
    assert_ne!(levels[0], levels[2]);
    assert_eq!(format!("{:?}", Timeliness::Strict), "Strict");
    assert_eq!(format!("{:?}", Timeliness::Timely), "Timely");
    assert_eq!(format!("{:?}", Timeliness::Unchecked), "Unchecked");
}

#[test]
fn timeliness__netdir_forwards_the_requested_level() {
    let p = Recorder::default();
    for level in [Timeliness::Strict, Timeliness::Unchecked, Timeliness::Timely] {
        assert!(p.netdir(level).is_err(), "this provider has no directory");
    }
    assert_eq!(
        p.seen(),
        vec![Timeliness::Strict, Timeliness::Unchecked, Timeliness::Timely]
    );
}

#[test]
fn timeliness__timely_netdir_is_an_alias_for_the_timely_level() {
    let p = Recorder::default();
    let _ = p.timely_netdir();
    assert_eq!(
        p.seen(),
        vec![Timeliness::Timely],
        "timely_netdir() must delegate to netdir(Timeliness::Timely)"
    );
}

#[test]
fn timeliness__arc_of_a_provider_is_itself_a_provider() {
    let p = Arc::new(Recorder::default());
    let as_provider: &dyn NetDirProvider = &p;
    let _ = as_provider.netdir(Timeliness::Strict);
    let _ = as_provider.timely_netdir();
    assert_eq!(p.seen(), vec![Timeliness::Strict, Timeliness::Timely]);
}

#[test]
fn timeliness__upcast_arc_produces_a_trait_object() {
    let p = Arc::new(Recorder::default());
    let dynamic: Arc<dyn NetDirProvider> = Arc::clone(&p).upcast_arc();
    let _ = dynamic.timely_netdir();
    let _ = dynamic.netdir(Timeliness::Unchecked);
    assert_eq!(p.seen(), vec![Timeliness::Timely, Timeliness::Unchecked]);
}

#[test]
fn timeliness__expired_directory_is_reported_as_expired() {
    assert_eq!(Error::DirExpired.kind(), ErrorKind::DirectoryExpired);
    assert_eq!(
        crate::common::shown(&Error::DirExpired),
        "Directory is expired, and we haven't got a new one yet"
    );
}

#[test]
fn timeliness__future_directory_is_reported_as_clock_skew() {
    assert_eq!(Error::DirNotYetValid.kind(), ErrorKind::ClockSkew);
    assert_eq!(
        crate::common::shown(&Error::DirNotYetValid),
        "Directory is published too far in the future: Your clock is probably wrong"
    );
}

#[test]
fn timeliness__absent_information_requires_bootstrap() {
    assert_eq!(Error::NoInfo.kind(), ErrorKind::BootstrapRequired);
    assert_eq!(Error::NotEnoughInfo.kind(), ErrorKind::BootstrapRequired);
    assert_eq!(
        crate::common::shown(&Error::NoInfo),
        "No directory information available"
    );
    assert_eq!(
        crate::common::shown(&Error::NotEnoughInfo),
        "Not enough directory information to build circuits"
    );
}

#[test]
fn timeliness__dirmgr_reexports_the_same_type() {
    /// Only compiles if `tor_dirmgr::Timeliness` is `tor_netdir::Timeliness`.
    fn takes(t: Timeliness) -> Timeliness {
        t
    }
    assert_eq!(takes(tor_dirmgr::Timeliness::Timely), Timeliness::Timely);
    assert_eq!(takes(tor_dirmgr::Timeliness::Strict), Timeliness::Strict);
}
